/*
 *
 * certauth_extensions.h - CA extensions for myproxy
 *
 */

#ifndef OPENSSL_NO_ENGINE
int initialise_openssl_engine(myproxy_server_context_t *server_context);
#endif

int is_certificate_authority_configured(myproxy_server_context_t
                                        *server_context);

void get_certificate_authority(myproxy_socket_attrs_t   *server_attrs,
			       myproxy_creds_t          *creds,
			       myproxy_request_t        *request,
			       myproxy_response_t       *response,
			       myproxy_server_context_t *server_context);

