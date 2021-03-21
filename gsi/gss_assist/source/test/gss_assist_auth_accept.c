/*
 * Copyright 1999-2006 University of Chicago
 * 
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 * 
 * http://www.apache.org/licenses/LICENSE-2.0
 * 
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "globus_common.h"
#include "gssapi.h"
#include "tokens_bsd.h"
#include "globus_gss_assist.h"

#define accept_message "ACCEPTOR WRAP MESSAGE"

#ifdef _WIN32
#define close closesocket
#undef perror
#define perror(m) do { \
        int wsa__last_error = WSAGetLastError(); \
        printf(m ": %d\n", wsa__last_error); \
    } while (0)
#endif

int main(int argc, char * argv[])
{
    gss_cred_id_t                       accept_cred = GSS_C_NO_CREDENTIAL;
    gss_cred_id_t                       delegated_init_cred 
        = GSS_C_NO_CREDENTIAL;
    OM_uint32                           major_status;
    OM_uint32                           minor_status;
    int                                 token_status;
    gss_ctx_id_t                        accept_context = GSS_C_NO_CONTEXT;
    OM_uint32                           ret_flags = 0;
    int                                 sock, connect_sock;
    char *                              print_buffer = NULL;
    char *                              recv_buffer = NULL;
    size_t                              buffer_length;
    struct sockaddr_in                  sockaddr;
    socklen_t                           length;
    char *                              init_name;
    char *                              verbose_env = NULL;
    
    globus_module_activate(GLOBUS_GSI_GSS_ASSIST_MODULE);

    verbose_env = getenv("GSS_ASSIST_VERBOSE_TEST");

    setbuf(stdout, NULL);
    
    sock = socket(AF_INET, SOCK_STREAM, 0);
    if(sock < 0)
    {
        perror("opening stream socket");
        exit(1);
    }

    sockaddr.sin_family = AF_INET;
    sockaddr.sin_addr.s_addr = htonl(INADDR_ANY);
    sockaddr.sin_port = 0;
    if(bind(sock, (struct sockaddr *) &sockaddr, sizeof(sockaddr)))
    {
        perror("binding stream socket");
        exit(1);
    }
    
    length = sizeof(sockaddr);
    if(getsockname(sock, (struct sockaddr *) &sockaddr, &length))
    {
        perror("getting socket name");
        exit(1);
    }
    /* Start accepting connection */
    listen(sock, 1);

    fprintf(stdout, "Socket has port #%d\n", ntohs(sockaddr.sin_port));

    connect_sock = accept(sock, 0, 0);
    if(connect_sock == -1) 
    {
        perror("accept");
        exit(1);
    }

    if(close(sock) < 0)
    {
        perror("Couldn't close listening socket");
        exit(1);
    }

    /* ACCEPTOR PROCESS */
    major_status = globus_gss_assist_acquire_cred(&minor_status,
                                                  GSS_C_ACCEPT,
                                                  &accept_cred);
    
    if(GSS_ERROR(major_status))
    {
        globus_gss_assist_display_status(
            stdout,
            "ACCEPTOR: Couldn't acquire acceptor's credentials",
            major_status,
            minor_status,
            0);
        exit(1);
    }

    major_status = globus_gss_assist_accept_sec_context(
        &minor_status,
        &accept_context,
        accept_cred,
        &init_name,
        &ret_flags,
        NULL,
        &token_status,
        &delegated_init_cred,
        token_bsd_get,
        (void *) (connect_sock),
        token_bsd_send,
        (void *) (connect_sock));
    if(GSS_ERROR(major_status))
    {
        globus_gss_assist_display_status(
            stdout,
            "ACCEPTOR: Couldn't authenticate as acceptor\n",
            major_status,
            minor_status,
            token_status);
        exit(1);
    }

    if(verbose_env)
    {
        fprintf(stdout, 
                "ACCEPTOR: "__FILE__":%d"
                ": Acceptor successfully created context"
                " for initiator: %s\n", __LINE__, init_name);
    }

    major_status = globus_gss_assist_get_unwrap(
        &minor_status,
        accept_context,
        &recv_buffer,
        &buffer_length,
        &token_status,
        token_bsd_get,
        (void *) (connect_sock),
        stdout);
    if(GSS_ERROR(major_status))
    {
        fprintf(stdout, "ACCEPTOR ERROR\n");
        globus_gss_assist_display_status(
            stdout,
            "ACCEPTOR: Couldn't get encrypted message from initiator\n",
            major_status,
            minor_status,
            token_status);
        fprintf(stdout, "ACCEPTOR ERROR FINISHED\n");
        exit(1);
    }

    print_buffer = malloc(buffer_length + 1);
    globus_libc_snprintf(print_buffer, buffer_length + 1, "%s", recv_buffer);

    if(verbose_env)
    {
        fprintf(stdout,
                "ACCEPTOR: "__FILE__":%d"
                ": received: %s\n", __LINE__, print_buffer);
    }

    free(print_buffer);
    free(recv_buffer);

    major_status = globus_gss_assist_wrap_send(
        &minor_status,
        accept_context,
        accept_message,
        sizeof(accept_message),
        &token_status,
        token_bsd_send,
        (void *) (connect_sock),
        stdout);
    if(GSS_ERROR(major_status))
    {
        globus_gss_assist_display_status(
            stdout,
            "ACCEPTOR: Couldn't encrypt and send message\n",
            major_status,
            minor_status,
            token_status);
        exit(1);
    }

    major_status = globus_gss_assist_get_unwrap(
        &minor_status,
        accept_context,
        &recv_buffer,
        &buffer_length,
        &token_status,
        token_bsd_get,
        (void *) (connect_sock),
        stdout);
    if(GSS_ERROR(major_status))
    {
        fprintf(stdout, "ACCEPTOR ERROR\n");
        globus_gss_assist_display_status(
            stdout,
            "ACCEPTOR: Couldn't get encrypted message from initiator\n",
            major_status,
            minor_status,
            token_status);
        fprintf(stdout, "ACCEPTOR ERROR FINISHED\n");
        exit(1);
    }

    print_buffer = malloc(buffer_length + 1);
    globus_libc_snprintf(print_buffer, buffer_length + 1, "%s", recv_buffer);

    if(verbose_env)
    {
        fprintf(stdout,
                "ACCEPTOR: "__FILE__":%d"
                ": received: %s\n", __LINE__, print_buffer);
    }

    free(print_buffer);
    free(recv_buffer);

    major_status = globus_gss_assist_wrap_send(
        &minor_status,
        accept_context,
        accept_message,
        sizeof(accept_message),
        &token_status,
        token_bsd_send,
        (void *) (connect_sock),
        stdout);
    if(GSS_ERROR(major_status))
    {
        globus_gss_assist_display_status(
            stdout,
            "ACCEPTOR: Couldn't encrypt and send message\n",
            major_status,
            minor_status,
            token_status);
        exit(1);
    }
                
    major_status = gss_delete_sec_context(&minor_status,
                                          &accept_context,
                                          GSS_C_NO_BUFFER);
    if(major_status != GSS_S_COMPLETE)
    {
        globus_gss_assist_display_status(
            stdout,
            "INITIATOR: Couldn't delete security context\n",
            major_status,
            minor_status,
            0);
        exit(1);
    }
            
    gss_release_cred(&minor_status,
                     &accept_cred);
    if(major_status != GSS_S_COMPLETE)
    {
        globus_gss_assist_display_status(
            stdout,
            "INITIATOR: Couldn't delete security context\n",
            major_status,
            minor_status,
            0);
        exit(1);
    }

    globus_module_deactivate(GLOBUS_GSI_GSS_ASSIST_MODULE);

    exit(0);
}
