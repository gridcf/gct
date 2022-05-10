#ifndef __MYPROXY_AUTHORIZATION_H
#define __MYPROXY_AUTHORIZATION_H

#include "myproxy_creds.h"
#include "myproxy_server.h"

typedef enum {
  AUTHORIZETYPE_NULL = 0,
  AUTHORIZETYPE_PASSWD,
  AUTHORIZETYPE_CERT,        /* uses SHA1 */
  AUTHORIZETYPE_SASL,
  AUTHORIZETYPE_CERT256,     /* uses SHA256 */
  AUTHORIZETYPE_NUMMETHODS
} author_method_t;

typedef enum {
  AUTHORIZEMETHOD_DISABLED,
  AUTHORIZEMETHOD_REQUIRED,
  AUTHORIZEMETHOD_SUFFICIENT
} author_status_t;

/* client/server authorization data */
typedef struct
{
  char *server_data;   /* data sent from the server.  It can be arbitrary ASCII
                          string ending with '\0'. */
  char *client_data;   /* data created by the client according to server_data */
  size_t client_data_len;
  author_method_t  method;
} authorization_data_t;

/* The methods argument should be an array of methods to prompt for,
   terminated by AUTHORIZETPYE_NULL. */
int authorization_init_server (authorization_data_t ***data,
                               author_method_t methods[]);
void authorization_data_free (authorization_data_t **data);
void authorization_data_free_contents (authorization_data_t *data);

char * authorization_get_name(author_method_t method);
author_method_t authorization_get_method(char *name);
author_status_t authorization_get_status(author_method_t method,
                                         struct myproxy_creds *creds,
                                         char *client_name,
                                         myproxy_server_context_t* config);

/*
 * Fill in author_data with client's response and return pointer into
 * author_data to data choosen by the client. No new space is allocated for
 * the returned pointer. This function is called by the server.
 * authorization_data_t is supposed to be allocated and (partly) filled in by
 * the server.
 */
authorization_data_t *
authorization_store_response(char *,
                             size_t,
                             author_method_t,
                             authorization_data_t **);

/*
 * Search a data for the supplied method in the supplied list. Using the extra
 * data fill in the response and return a pointer into the list to the data
 * choosen. No special space is allocated for the return value.
 * Called by the client.
 */
authorization_data_t *
authorization_create_response(authorization_data_t **,
                              author_method_t,
                              void *extra_data,
                              size_t extra_data_len);
/*
 * Verifies that data sent by the client matches the expecting value for the
 * server's challenge.  Returns 1 on success, 0 on failure.
 */
int authorization_check(authorization_data_t *client_auth_data,
                        struct myproxy_creds *creds,
                        char *client_name);

int authorization_check_ex(authorization_data_t *client_auth_data,
                           struct myproxy_creds *creds,
                           char *client_name,
                           myproxy_server_context_t *config);

#endif /* __MYPROXY_AUTHORIZATION_H */
