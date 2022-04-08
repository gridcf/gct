#include "myproxy_common.h"

void get_voms_proxy(myproxy_socket_attrs_t *attrs,
                    myproxy_creds_t *creds,
                    myproxy_request_t *request,
                    myproxy_response_t *response,
                    myproxy_server_context_t *config);


int voms_init_delegation(myproxy_socket_attrs_t *attrs,
                         const char *delegfile,
                         const int lifetime_seconds,
                         char *passphrase,
                         char *voname, char *vomses, char *voms_userconf);


int voms_contact(SSL_CREDENTIALS *creds, int lifetime, 
                 char *voname, char *vomses, char *voms_userconf,
                 unsigned char **aclist, int *aclist_length);



static void
voms_put_error_message(struct vomsdata *vd, int err)
{
    char *error_message = NULL;
    error_message = VOMS_ErrorMessage(vd, err, NULL, 0);
    if (error_message != NULL) {
        myproxy_debug("%s", error_message);
        free(error_message);
    }
}

/*
 * get the user info for specified vo
 */
static int
voms_get_user_info(struct vomsdata *vd, 
                   voms_command_t *command,
                   char *vomses_path)
{
    int return_code = 1;
    int i;
    int result = 0, err = 0;
    struct contactdata **servers = NULL;

    servers = VOMS_FindByAlias(vd, command->vo, NULL, vomses_path, &err);
    if (servers == NULL) {
        verror_put_string("Error finding voms server info.");
        voms_put_error_message(vd, err);
        goto done;
    }

    myproxy_debug("Retrieve %s VO", command->vo);
    for (i = 0; servers[i] != NULL; i++) {
        struct contactdata *info = servers[i];
        myproxy_debug("Contact to VOMS Server: %s", info->host);
        result = VOMS_Contact(info->host,
                              info->port,
                              info->contact,
                              command->command,
                              vd,
                              &err); 
        if (result) {
            /* if contact succeded jumps to other VOs */
            return_code = 0;
            break;
        }

        myproxy_debug("Failed to contact: %s", info->host);
        voms_put_error_message(vd, err);
    }

    if (servers != NULL) {
        VOMS_DeleteContacts(servers);
    }

  done:
    return return_code;
}

static int
credential_write_to_temporary(SSL_CREDENTIALS *creds, char *template)
{
    int fd = -1;
    int return_status = 1;
    unsigned char *buffer = NULL;
    int buffer_len;

    assert(creds != NULL);
    assert(template != NULL);

    fd = mkstemp(template);
    if (fd == -1) {
        verror_put_string("Error creating %s", template);
        verror_put_errno(errno);
        goto done;
    }

    if (ssl_proxy_to_pem(creds, &buffer, &buffer_len, NULL) == SSL_ERROR) {
        goto error;
    }

    if (write(fd, buffer, buffer_len) == -1) {
        verror_put_errno(errno);
        verror_put_string("Error writing proxy to %s", template);
        goto error;
    }

    return_status = SSL_SUCCESS;

 error:
    if (buffer != NULL) {
        free(buffer);
    }

    if (fd != -1) {
        if (close(fd) < 0) {
            verror_put_errno(errno);
            return_status = SSL_ERROR;
        }
        if (return_status == SSL_ERROR) {
            ssl_proxy_file_destroy(template);
        }
    }

  done:
    return return_status;
}

static int
vomses_write_to_temporary(char *vomses, char *template)
{
    int fd = -1;
    int return_status = 1;

    assert(vomses != NULL);

    fd = mkstemp(template);
    if (fd == -1) {
        verror_put_string("Error creating %s", template);
        verror_put_errno(errno);
        goto done;
    }

    if (write(fd, vomses, strlen(vomses)) == -1) {
        verror_put_errno(errno);
        verror_put_string("Error writing vomses to %s", template);
        goto error;
    }

    return_status = 0;

  error:
    if (fd != -1) {
        if (close(fd) < 0) {
            verror_put_errno(errno);
            return_status = 1;
        }
        if (return_status != 0) {
            unlink(template);
        }
    }

  done:

    return return_status; 
}

static int
decide_proxy_lifetime(myproxy_request_t *request,
                      myproxy_creds_t *creds,
                      myproxy_server_context_t *config)
{
    int max_proxy_lifetime = config->max_proxy_lifetime;
    int lifetime = 0;
    if (request->proxy_lifetime > 0) {
        lifetime = request->proxy_lifetime;
    }
    if (creds->lifetime > 0) {
        if (lifetime > 0) {
            lifetime = MIN(lifetime, creds->lifetime);
        } else {
            lifetime = creds->lifetime;
        }
    }
    if (max_proxy_lifetime > 0) {
        if (lifetime > 0) {
            lifetime = MIN(lifetime, max_proxy_lifetime);
        } else {
            lifetime = max_proxy_lifetime;
        }
    }
    return lifetime;
}

static char *
voms_get_role_command(const char *str)
{
    char *buf = NULL;
    char *p_role = NULL;
    size_t buf_len, role_len;; 
    int i = 0;

    if ((str == NULL) || (str[0] == '\0')) {
        return NULL;
    }

    p_role = strstr(str, "/Role=");
    if ((p_role == NULL) || (p_role != str)) {
        return NULL;
    }
    p_role += 6;
    if (p_role[0] == '\0') {
        return NULL;
    }
    role_len = strlen(p_role);
    buf_len = role_len + 2;
    buf = (char *)malloc(buf_len);
    if (buf == NULL) {
        return NULL;
    }
    memset(buf, '\0', buf_len);

    buf[i++] = 'R';
    memcpy(&buf[i], p_role, role_len);

    return buf;
}

static char *
voms_get_mapping_command(const char *str)
{
    char *buf = NULL;
    char *p_role = NULL;
    size_t buf_len = 0, group_len = 0, role_len = 0;
    int i = 0;

    if ((str == NULL) || (str[0] == '\0')) {
        return NULL;
    }

    p_role = strstr(str, "/Role=");
    if (p_role == NULL) {
        return NULL;
    }
    group_len = p_role - str;
    buf_len = 0;
    p_role += 6;
    if (p_role[0] == '\0') {
        return NULL;
    }
    role_len = strlen(p_role);
    buf_len = group_len + role_len;
    if (str[0] == '/') {
        buf_len += 3; /* 'B',':','\0' */
    } else {
        buf_len += 4; /* 'B','/',':','\0' */
    }

    buf = (char *)malloc(buf_len);
    if (buf == NULL) {
        return NULL;
    }
    memset(buf, '\0', buf_len);

    buf[i++] = 'B';
    if (str[0] != '/') {
        buf[i++] = '/';
    }
    memcpy(&buf[i], str, group_len);
    i += group_len;
    buf[i++] = ':';
    memcpy(&buf[i], p_role, role_len);

    return buf;
}

static char *
voms_get_group_command(const char *str)
{
    char *buf = NULL;
    size_t buf_len = 0, str_len = 0; 
    int i = 0;

    if ((str == NULL) || (str[0] == '\0')) {
        return NULL;
    }

    str_len = strlen(str);
    if (str[0] == '/') {
        buf_len = str_len + 2;
    } else {
        buf_len = str_len + 3;
    }

    buf = (char *)malloc(buf_len);
    if (buf == NULL) {
        return NULL;
    }
    memset(buf, '\0', buf_len);

    buf[i++] = 'G';
    if (str[0] != '/') {
       buf[i++] = '/';
    }
    memcpy(&buf[i], str, str_len);
    buf_len = strlen(buf);
    if (buf[buf_len-1] == '/') {
        buf[buf_len-1] = '\0';
    }
    return buf;
}

static char *
voms_convert_command(const char *str)
{
    char *p = NULL;
    char *result = NULL;

    if (str == NULL) {
        return NULL;
    }

    p = strstr(str, "/Capability=");
    if (p != NULL) {
        verror_put_string("Error capability selection not supported");
        return NULL;
    }

    p = strstr(str, "/Role=");
    if (p != NULL) {
        if (p == str) {
            result = voms_get_role_command(str);
        } else {
            result = voms_get_mapping_command(str);
        }
    } else {
        result = voms_get_group_command(str);
    }
    return result;
}

static int 
voms_parse_command(const char *voms, char **vo, char **command)
{
    int result = 1;
    char *p_colon = NULL;
    p_colon = strchr(voms, ':');
    if (p_colon == NULL) {
        *vo = strdup(voms);
        if (*vo == NULL) {
            goto error;
        }
        *command = voms_convert_command(voms);
        if (*command == NULL) {
            goto error;
        }
    } else {
        size_t vo_len = p_colon - voms;
        *vo = (char *)malloc(vo_len+1);
        if (*vo == NULL) {
            goto error;
        }
        strncpy(*vo, voms, vo_len);
        (*vo)[vo_len] = '\0';
        *command = voms_convert_command(p_colon+1);
        if (*command == NULL) {
            goto error;
        }
    }
    result = 0;

  error:
    if ((result == 1) && (*vo != NULL)) {
        free(*vo);
    }
    if ((result == 1) && (*command != NULL)) {
        free(*command);
    }

    return result;
}

static voms_command_t *
voms_command_list_find(voms_command_t *head, const char *vo)
{
    if (head == NULL) {
        return NULL;
    }

    voms_command_t *curr = head;
    while (curr != NULL) {
        if (strcmp(curr->vo, vo) == 0) {
            break;
        }
        curr = curr->next;
    }
    return curr;
}

static voms_command_t *
voms_command_new(const char *vo, const char *cmd)
{
    voms_command_t *node = NULL;
    node = (voms_command_t *)malloc(sizeof(voms_command_t));
    if (node == NULL) {
        return NULL ;
    }
    node->vo = strdup(vo);
    node->command = strdup(cmd);
    node->next = NULL;
    return node;
}

static void
voms_command_list_free(voms_command_t *head)
{
    if (head == NULL) {
        return ;
    }

    voms_command_t *current = head;
    while (current != NULL) {
        voms_command_t *next = current->next;
        if (current->vo != NULL) {
            free(current->vo);
        }
        if (current->command != NULL) {
            free(current->command);
        }
        free(current);
        current = next;
    }
}

static int
voms_command_list_add(voms_command_t **headRef, const char *vo, const char *cmd)
{
    int result = 1;
    voms_command_t *node = NULL;

    voms_command_t *current = *headRef;
    if (current == NULL) {
        node = voms_command_new(vo, cmd);
        if (node != NULL) {
            *headRef = node;
            result = 0;
        }
    } else {
        node = voms_command_list_find(current, vo); 
        if (node != NULL) {
            /* Append command to the node */
            my_append(&(node->command), ",", cmd, NULL);
            result = 0;
        } else { 
            /* Create and Add a new node to last */
            node = voms_command_new(vo, cmd);
            if (node != NULL) {
                while(current->next != NULL) {
                    current = current->next;
                }
                current->next = node;
                result = 0;
            }
        }
    }

    return result;
}

static voms_command_t *
voms_command_list_new(const char *voname)
{
    char *wk_voname = NULL;
    char *token     = NULL;
    voms_command_t *head = NULL;
    int result = 1;

    wk_voname = strdup(voname);
    if (wk_voname == NULL) {
        goto done;
    }

    token = strtok(wk_voname, "\n");
    while (token != NULL) {
        int parse_result = 1;
        int add_result = 1;
        char *vo = NULL, *cmd = NULL;
        parse_result = voms_parse_command(token, &vo, &cmd);
        if (parse_result) {
            verror_put_string("Error voms_parse_command");
            goto error;
        }
        add_result = voms_command_list_add(&head, vo, cmd);
        if (vo)  free(vo);
        if (cmd) free(cmd);
        if (add_result) {
            verror_put_string("Error voms_command_list_add");
            goto error;
        }
        token = strtok(NULL, "\n");
    }

    result = 0;

  error:
    if (wk_voname != NULL) {
        free(wk_voname);
    }
    if ((result == 1) && (head != NULL)) {
        voms_command_list_free(head);
    }

  done:
    return head;
}

static int
get_AC_SEQ(struct vomsdata *vd, unsigned char **aclist, int *aclist_length)
{
    int result = 1;
    int j;
    int len = 0;
    AC_SEQ *acseq = NULL;

    acseq = AC_SEQ_new();
    if (acseq == NULL) {
        verror_put_string("Couldn't allocate AC_SEQ");
        goto error;
    }

    for (j = 0; vd->data[j] != NULL; j++) {
        AC *ac = VOMS_GetAC( vd->data[j] );
        if (ac == NULL) {
            verror_put_string("VOMS_GetAC failed.");
        } else {
            if (! sk_AC_push(acseq->acs, ac) ) {
                verror_put_string("sk_AC_push failed");
            }
        }
    }

    /* convert AC_SEQ to DER-form */
    len = i2d_AC_SEQ(acseq, NULL);
    if (len < 0) {
        verror_put_string("i2d_AC_SEQ return nagative value");
    } else {
        unsigned char *p = NULL;
        p = (unsigned char*)malloc(len);
        if (p == NULL) {
            verror_put_string("Couldn't allocate for AC_SEQ");
        } else {
            *aclist = p;
            i2d_AC_SEQ(acseq, &p);
        }
    }
    *aclist_length = len;
    if (aclist == NULL) {
        verror_put_string("Couldn't get User's info from voms servers");
        goto error;
    }

    result = 0;

  error:
    if (acseq != NULL) {
        AC_SEQ_free(acseq);
    }

    return result;
}



/*
 * Get VOMS User info
 * 
 * @param aclist DER-encoded AC-sequence
 * @param aclist_length length of aclist
 * Returns 0 on success or 1 on error.
 */
int 
voms_contact(SSL_CREDENTIALS *creds, int lifetime, 
             char *voname, char *vomses, char *voms_userconf,
             unsigned char **aclist, int *aclist_length)

{

    int return_code = 1;
    int verify_ac = 0;
    struct vomsdata *vd = NULL; 
    int err;
    int result = 1;
    int is_write_temp_vomses = 0;
    char *old_ucert = NULL, *old_ukey = NULL;
    char *tmp_dir = "/tmp/";
    char *cred_path = NULL;
    char *vomses_path = NULL;
    voms_command_t *vo_list = NULL;
    voms_command_t *current = NULL;

    if (voname == NULL) {
        verror_put_string("NULL voname passed to function");
        goto done;
    }

    vd = VOMS_Init(DEFAULT_VOMS_DIR, DEFAULT_CACERT_DIR);
    if (vd == NULL) {
        verror_put_string("VOMS_Init failed.");
        goto done;
    }

    if ( ssl_creds_certificate_is_proxy(creds) ) {
        myproxy_debug("Stored Credential is Proxy. VOMS AC doesn't verify.");
        verify_ac = VERIFY_NONE;
    } else {
        verify_ac = VERIFY_FULL;
    }
    result = VOMS_SetVerificationType(verify_ac, vd, &err);
    if (! result) {
        verror_put_string("VOMS_SetVerificationType is failed.");
        voms_put_error_message(vd, err);
        goto error;
    }

    result = VOMS_SetLifetime(lifetime, vd, &err);
    if (result == 0) {
        verror_put_string("VOMS_SetLifeime is failed");
        goto error;
    }

    /* Get contactdata */
    if (vomses != NULL) {
        if (my_append(&vomses_path, tmp_dir, "vomses-tmp.XXXXXX", NULL) < 0) {
            verror_put_string("Error creating vomses_path");
            goto error;
        }
        if ( vomses_write_to_temporary(vomses, vomses_path) != 0 ) {
            verror_put_string("Couldn't create temporary vomses");
            goto error;
        }
        is_write_temp_vomses = 1;
    } else {
        if (voms_userconf == NULL) { 
            verror_put_string("No VOMS Server Information");
            goto error;
        }
        vomses_path = strdup(voms_userconf);
        if (vomses_path == NULL) {
           verror_put_string("Error duplicating voms_userconf");
           goto error;
        }
    }

    if ( my_append(&cred_path, tmp_dir, "x509up_uXXXXXX", NULL) < 0) {
        verror_put_string("Error creating cred_path");
        goto error;
    }

    /* Set X509_USER_CERT, X509_USER_KEY */
    old_ucert = getenv("X509_USER_CERT");
    old_ukey  = getenv("X509_USER_KEY");
    /* 
        Save credential (cert & private key) to cred_path 
        cred_path is modified on success.
     */
    if ( credential_write_to_temporary(creds, cred_path) != SSL_SUCCESS ) {
       verror_put_string("Couldn't store proxy to %s", cred_path);
       goto error;
    }
    setenv("X509_USER_CERT", cred_path, 1);
    setenv("X509_USER_KEY", cred_path, 1);

    /* Contact to VOMS server */
    vo_list = voms_command_list_new(voname);
    if (vo_list == NULL) {
        verror_put_string("Error voms_command_list_new");
        goto error;
    }

    myproxy_debug("retrieving VOMS User Information.");
    for (current = vo_list; current != NULL; current = current->next) {
        if ( voms_get_user_info(vd, current, vomses_path) != 0) {
            verror_put_string("Couldn't get user information for %s VO.", current->vo);
            goto error;
        }
    }

    /* Get User's Info */
    if (vd->data == NULL) {
        verror_put_string("Error User's info is NULL.");
        goto error;
    }

    if (get_AC_SEQ(vd, aclist, aclist_length) != 0) {
        verror_put_string("Error get_AC_SEQ");
        goto error;
    }

    /* Success */
    return_code = 0;

  error:
    if (vd != NULL) {
        VOMS_Destroy(vd);
    }

    if (vo_list != NULL) {
        voms_command_list_free(vo_list);
    }

    if (cred_path != NULL) {
        /* destroy tmporary proxy */
        ssl_proxy_file_destroy(cred_path);
        free(cred_path);
    }

    if (vomses_path != NULL) {
        if (is_write_temp_vomses == 1) {
            unlink(vomses_path);
        }
        free(vomses_path);
    }

    if (old_ucert != NULL) {
       setenv("X509_USER_CERT", old_ucert, 1);
    } else {
       unsetenv("X509_USER_CERT");
    }

    if (old_ukey != NULL) {
       setenv("X509_USER_KEY", old_ukey, 1);
    } else {
       unsetenv("X509_USER_KEY");
    }

  done:

    return return_code;
}

/* Delegate requested credentials to the client */
void get_voms_proxy(myproxy_socket_attrs_t *attrs,
                    myproxy_creds_t *creds,
                    myproxy_request_t *request,
                    myproxy_response_t *response,
                    myproxy_server_context_t *config)
{

    int lifetime = 0;
    lifetime = decide_proxy_lifetime(request, creds, config);

    if (voms_init_delegation(attrs, creds->location,
                             lifetime,
                             request->passphrase,
                             request->voname,
                             request->vomses, 
                             config->voms_userconf) < 0) {
        response->response_type = MYPROXY_ERROR_RESPONSE;
        response->error_string = strdup( verror_get_string() );
    } else {
        myproxy_log("Delegating credentials for %s lifetime=%d",
                    creds->owner_name, lifetime);
        response->response_type = MYPROXY_OK_RESPONSE;
    }
    return ;
}

static X509_EXTENSION *
voms_create_AC_SEQ_X509_EXTENSION(unsigned char *acseq, int acseq_length)
{
    ASN1_OCTET_STRING *ac_DER_string = NULL;
    X509_EXTENSION    *ext = NULL;

    ac_DER_string = ASN1_OCTET_STRING_new();
    if (ac_DER_string == NULL) {
        verror_put_string("Couldn't create new ASN.1 octet string for the AC");
        goto error;
    }

    ac_DER_string->data = (unsigned char*)malloc(acseq_length);
    if (ac_DER_string->data == NULL) {
        verror_put_string("Couldn't allocate ASN1_OCTET");
        goto error;
    }
    memcpy(ac_DER_string->data, acseq, acseq_length);
    ac_DER_string->length = acseq_length;
    ext = X509_EXTENSION_create_by_NID(NULL, OBJ_txt2nid("acseq"),
                                       0, ac_DER_string);
    if (ext == NULL) {
        ssl_error_to_verror();
        goto error;
    }

  error:
    if (ac_DER_string != NULL) {
        ASN1_OCTET_STRING_free(ac_DER_string);
    }

    return ext;
}

static int
voms_contact_ext(const char *source_credentials, const int lifetime,
                 char *passphrase, 
                 char *voname, char *vomses, char *voms_userconf)
{ 
    int result = 1;
    SSL_CREDENTIALS *creds = NULL; 
    unsigned char   *acseq= NULL;
    int             acseq_length = 0;
    X509_EXTENSION  *ext = NULL;

    /* Load proxy we are going to use to contact voms server. */
    creds = ssl_credentials_new();
    if (creds == NULL) {
        goto done;
    }
    if (ssl_proxy_load_from_file(creds, source_credentials, 
                                 passphrase) == SSL_ERROR) {
        goto done;
    }

    /* Get VOMS UserInfo  */
    if ( voms_contact(creds, lifetime, voname, vomses, voms_userconf,
                      &acseq, &acseq_length) ) {
        goto error;
    }

    /* Cerate X509_Extension */
    ext = voms_create_AC_SEQ_X509_EXTENSION(acseq, acseq_length);
    if (ext == NULL) {
        verror_put_string("Couldn't create AC_SEQ extension.");
        goto error;
    }
    if ( myproxy_add_extension(ext) != 0) {
        verror_put_string("Couldn't add AC_SEQ to myproxy_extensions.");
        goto error;
    }

    result = 0;

  error:
    if (acseq != NULL) {
        free(acseq);
    }
    if (creds != NULL) {
        ssl_credentials_destroy(creds);
    }
    if (ext != NULL) {
        X509_EXTENSION_free(ext);
    }

  done:
    return result;
}


int
voms_init_delegation(myproxy_socket_attrs_t *attrs,
                     const char *delegfile,
                     const int lifetime,
                     char *passphrase,
                     char *voname, char *vomses, 
                     char *voms_userconf)
{

    char error_string[1024];

    if (attrs == NULL)
        return -1;
    if (voname == NULL)
        return -1;


    if (voms_contact_ext(delegfile, lifetime, passphrase, 
                          voname, vomses, voms_userconf))
    {
        verror_put_string("Couldn't get VOMS User Information.");
        return -1;
    }

    if (GSI_SOCKET_delegation_init_ext(attrs->gsi_socket,
                                       delegfile,
                                       lifetime,
                                       passphrase) == GSI_SOCKET_ERROR)
    {
        GSI_SOCKET_get_error_string(attrs->gsi_socket, error_string,
                                    sizeof(error_string));
        myproxy_log_verror(); verror_clear();
        verror_put_string("Error delegating credentials: %s\n", error_string);
        return -1;
    }
    return 0;
}
