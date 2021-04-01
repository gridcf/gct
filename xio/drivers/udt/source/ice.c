/*
 * Copyright 2013 University of Chicago
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

/* Library for adding ICE to an existing application using libnice. Designed
 * to work with libnice 0.0.9 or later and glib2 2.22.5 or later.
 *
 * Build with:
 *
 *   gcc -c -o ice.o ice.c `pkg-config --cflags --libs nice`
 *
 */
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#define GLIB_VERSION_MIN_REQUIRED GLIB_VERSION_2_30
#include <glib.h>
#if NICE_VERSION_AT_LEAST_0_1_2
#include <gio/gio.h>
#endif

#include "ice.h"

#define THIS_FILE "ice.c"

#define MAX_HOST_CANDS 4

static int lib_initialized;


/* nice callbacks and event thread */
static void * thread_mainloop(void *data);
static void cb_candidate_gathering_done(NiceAgent *agent, guint stream_id,
                                        gpointer data);
static void cb_component_state_changed(NiceAgent *agent, guint stream_id,
                                       guint component_id, guint state,
                                       gpointer data);
static void cb_new_selected_pair(NiceAgent *agent, guint stream_id,
                                 guint component_id, gchar *lfoundation,
                                 gchar *rfoundation, gpointer data);
static void cb_nice_recv(NiceAgent *agent, guint stream_id, guint component_id,
                         guint len, gchar *buf, gpointer data);

/* helper functions */
static int snprint_cand(char *out, size_t outlen, const NiceCandidate *cand);
static NiceCandidate *parse_candidate(char *scand, guint stream_id);
static int nice_p_address_safe_copy(NiceAddress *naddr, struct sockaddr *saddr,
                                    socklen_t *addrlen);
static NiceCandidate *find_candidate(GSList *candidates,
                                     const gchar *foundation);
static void free_candidates(GSList *candidates);
static const char *get_cand_type_name(NiceCandidateType nct);
static const char *get_state_name(guint state);
static int dup_socket(int sock);


/**
 * Perform any necessary library initialization. Should only be called
 * from one thread.
 */
int ice_lib_init() {
    if (lib_initialized)
        return 0;
    g_type_init();
    g_thread_init(NULL);
    lib_initialized = 1;
    return 1;
}


void ice_lib_shutdown() {
    lib_initialized = 0;
}


/*
 * Initialize and ICE session using the provided stun server and mode
 * (controlling or controlled). This will contact the stun server to get
 * srflx candidates, gather host candidates, and initialize a session.
 * Once this is called, ice_get_local_data can be called.
 *
 * If ICE_SUCCESS is returned, caller is responsible for calling ice_destory.
 */
int ice_init(struct icedata *ice_data,
             const char *stun_host, unsigned int stun_port,
             int controlling) {
#ifdef NICE_AGENT_GATHER_CANDIDATES_RETURNS_GBOOLEAN
    gboolean ok;
#endif

    if (!lib_initialized)
        return ICE_FAILURE;

    ice_data->gcontext = NULL;
    ice_data->gloop = NULL;
    ice_data->gloopthread = NULL;
    ice_data->agent = NULL;
    ice_data->state_mutex = NULL;
    ice_data->gather_cv = NULL;
    ice_data->negotiate_cv = NULL;
    ice_data->bind_addr = NULL;
    ice_data->remote_addr = NULL;

    ice_data->gather_done = FALSE;
    ice_data->negotiate_done = FALSE;
    ice_data->selected_pair_done = FALSE;

    ice_data->state_mutex = g_mutex_new();
    ice_data->gather_cv = g_cond_new();
    ice_data->negotiate_cv = g_cond_new();

    ice_data->gcontext = g_main_context_new();
    if (ice_data->gcontext == NULL) {
        goto error;
    }

    ice_data->gloop = g_main_loop_new(ice_data->gcontext, FALSE);
    if (ice_data->gloop == NULL) {
        goto error;
    }

#ifdef HAVE_NICE_COMPATIBILITY_RFC5245
    ice_data->agent = nice_agent_new(ice_data->gcontext,
                                     NICE_COMPATIBILITY_RFC5245);
#else
    ice_data->agent = nice_agent_new(ice_data->gcontext,
                                     NICE_COMPATIBILITY_DRAFT19);
#endif
    if (ice_data->agent == NULL) {
        goto error;
    }

    g_signal_connect(G_OBJECT(ice_data->agent), "candidate-gathering-done",
                     G_CALLBACK(cb_candidate_gathering_done), ice_data);

    g_signal_connect(G_OBJECT(ice_data->agent), "new-selected-pair",
                     G_CALLBACK(cb_new_selected_pair), ice_data);

    g_signal_connect(G_OBJECT(ice_data->agent), "component-state-changed",
                     G_CALLBACK(cb_component_state_changed), ice_data);

    g_object_set(G_OBJECT(ice_data->agent),
                 "controlling-mode", controlling, NULL);

    g_object_set(G_OBJECT(ice_data->agent), "stun-server", stun_host, NULL);
    g_object_set(G_OBJECT(ice_data->agent),
                 "stun-server-port", stun_port, NULL);

    g_object_set(G_OBJECT(ice_data->agent), "upnp", FALSE, NULL);

    ice_data->stream_id = nice_agent_add_stream(ice_data->agent, 1);
    if (ice_data->stream_id == 0) {
        return ICE_FAILURE;
    }

    nice_agent_attach_recv(ice_data->agent, ice_data->stream_id, 1,
                           ice_data->gcontext, cb_nice_recv, ice_data);

#ifdef NICE_AGENT_GATHER_CANDIDATES_RETURNS_GBOOLEAN
    ok = nice_agent_gather_candidates(ice_data->agent, ice_data->stream_id);
    if (!ok) {
        goto error;
    }
#else
    nice_agent_gather_candidates(ice_data->agent, ice_data->stream_id);
#endif

    g_debug("starting event thread");
    ice_data->gloopthread = g_thread_create(&thread_mainloop, ice_data->gloop,
                                            TRUE, NULL);
    if (ice_data->gloopthread == NULL) {
        goto error;
    }

    g_debug("waiting for candidate gathering");
    g_mutex_lock(ice_data->state_mutex);
    while (!ice_data->gather_done)
        g_cond_wait(ice_data->gather_cv, ice_data->state_mutex);
    g_mutex_unlock(ice_data->state_mutex);
    g_debug("gathering done");

    /* TODO: make sure we found at least one candidate
    */
    return ICE_SUCCESS;

error:
    ice_destroy(ice_data);

    return ICE_FAILURE;
}


/* 
 * Get a string in out containing ufrag, password, and all local candidates.
 * This should be passed to ice_negotiate on the other end, parsed into
 * argc/argv using spaces.
 *
 * ice_init must be called first.
 */
int ice_get_local_data(struct icedata *ice_data, char *out, size_t outsize) {
    gboolean ok;
    int result = ICE_FAILURE;
    size_t ufraglen, passwordlen;
    gchar *local_ufrag = NULL;
    gchar *local_password = NULL;
    char *p = out;
    int written;
    GSList *cand=NULL, *item=NULL;
    NiceCandidate *c;

    ok = nice_agent_get_local_credentials(ice_data->agent, 1,
                                          &local_ufrag, &local_password);
    if (!ok)
        goto end;

    ufraglen = strlen(local_ufrag);
    passwordlen = strlen(local_password);
    if (outsize < ufraglen + passwordlen + 2) {
        return ICE_FAILURE;
    }

    /* Note: snprintf return value does not include null byte */
    written = snprintf(out, outsize, "%s %s",
                       local_ufrag, local_password);
    if (written < 0 || (size_t)written >= outsize) {
        /* buffer too small */
        return ICE_FAILURE;
    }
    outsize -= written;
    p += written;

    cand = nice_agent_get_local_candidates(ice_data->agent,
                                           ice_data->stream_id, 1);
    if (cand == NULL) {
        goto end;
    }

    for (item = cand; item; item = item->next) {
        c = (NiceCandidate *)item->data;
        if (outsize < 2)
            return ICE_FAILURE;

        /* only allow ipv4 until udt driver support v6 */
#if HAVE_NICE_ADDRESS_IP_VERSION
        if(nice_address_ip_version(&c->addr) != 4)
            continue;
#else
        if(c->addr.s.addr.sa_family != AF_INET)
            continue;
#endif

        snprintf(p, outsize, " ");
        outsize--;
        p++;
        written = snprint_cand(p, outsize, c);
        if (written < 0 || (size_t)written >= outsize) {
            /* buffer too small */
            return ICE_FAILURE;
        }
        outsize -= written;
        p += written;
    }
    result = ICE_SUCCESS;

end:
    if (local_ufrag)
        g_free(local_ufrag);
    if (local_password)
        g_free(local_password);
    if (cand) {
        free_candidates(cand);
    }
    return result;
}


/*
 * Start ICE negotiation and block until it's complete, using remote
 * data parsed into args - rdata array with argc strings.
 *
 * After negotiation is successful, use ice_get_negotiated_addrs or
 * ice_get_negotiated_sock.
 */
int ice_negotiate(struct icedata *ice_data, int argc, char *rdata[]) {
    int i, status;
    gboolean ok;
    NiceCandidate *c;
    gchar ufrag[80];
    gchar password[80];
    GSList *cands = NULL;

    if (argc < 3) {
        return ICE_FAILURE;
    }

    /* First args are ufrag and password. */
    strncpy(ufrag, rdata[0], sizeof(ufrag));
    ufrag[sizeof(ufrag) - 1] = '\0';
    strncpy(password, rdata[1], sizeof(password));
    password[sizeof(password) - 1] = '\0';

    g_debug("remote: ufrag='%s' password='%s'", ufrag, password);

    /* Remaining args are serialized canidates (at least one is required) */
    for (i=2; i < argc; i++) {
        c = parse_candidate(rdata[i], ice_data->stream_id);
        if (c == NULL) {
            goto end;
        }

        /* only allow ipv4 until udt driver support v6 */
#if HAVE_NICE_ADDRESS_IP_VERSION
        if(nice_address_ip_version(&c->addr) != 4) {
            nice_candidate_free(c);
            continue;
        }
#else
        if(c->addr.s.addr.sa_family != AF_INET) {
            nice_candidate_free(c);
            continue;
        }
#endif
        
        cands = g_slist_prepend(cands, c);
    }
    cands = g_slist_reverse(cands);

    ok = nice_agent_set_remote_credentials(ice_data->agent,
                                           ice_data->stream_id,
                                           ufrag, password);
    if (!ok) {
        g_message("failed to set remote credentials");
        goto end;
    }

    g_debug("remote cand count: %d\n", g_slist_length(cands));
    status = nice_agent_set_remote_candidates(ice_data->agent,
                                              ice_data->stream_id, 1,
                                              cands);
    if (status < 1) {
        g_message("failed to set remote candidates: %d", status);
        goto end;
    }

    g_debug("waiting for ice negotiation");
    g_mutex_lock(ice_data->state_mutex);
    while (!ice_data->negotiate_done)
        g_cond_wait(ice_data->negotiate_cv, ice_data->state_mutex);
    g_mutex_unlock(ice_data->state_mutex);
    g_debug("negotiation finished");


end:
    if (cands)
        free_candidates(cands);

    if (ice_data->selected_pair_done)
        return ICE_SUCCESS;

    return ICE_FAILURE;
}


/*
 * Copies local base_addr (for bind) and remote addr (destination) to the
 * output params.
 */
int ice_get_negotiated_addrs(struct icedata *ice_data,
                             struct sockaddr *laddr, socklen_t *laddrlen,
                             struct sockaddr *raddr, socklen_t *raddrlen) {
    int rval;

    if (!ice_data->selected_pair_done)
        return ICE_FAILURE;

    rval = nice_p_address_safe_copy(ice_data->bind_addr, laddr, laddrlen);
    if (rval != ICE_SUCCESS)
        return rval;
    rval = nice_p_address_safe_copy(ice_data->remote_addr, raddr, raddrlen);
    if (rval != ICE_SUCCESS)
        return rval;

    return ICE_SUCCESS;
}

#if !defined(HAVE_NICESOCKET) && !defined(HAVE_NICE_AGENT_GET_SELECTED_SOCKET)
/* hack to get access to private ICE socket */
typedef struct _NiceSocket NiceSocket;

struct _NiceSocket
{
  NiceAddress addr;
#if NICE_VERSION_AT_LEAST_0_1_2
  GSocket *fileno;
#else
  guint fileno;
#endif
  gint (*recv) (NiceSocket *sock, NiceAddress *from, guint len,
      gchar *buf);
  gboolean (*send) (NiceSocket *sock, const NiceAddress *to, guint len,
      const gchar *buf);
  gboolean (*is_reliable) (NiceSocket *sock);
  void (*close) (NiceSocket *sock);
  void *priv;
};
#endif

/*
 * Duplicate the internal socket associated with the selected pair,
 * and set in the @sock_dup out parameter.
 */
int ice_get_negotiated_sock(struct icedata *ice_data, int *sock_dup) {
#ifndef HAVE_NICE_AGENT_GET_SELECTED_SOCKET
    NiceSocket *nice_socket;
#endif
    int fd;

    if (!ice_data->selected_pair_done)
        return ICE_FAILURE;

#if NICE_VERSION_AT_LEAST_0_1_2
    GSocket *gsock;

#   if HAVE_NICE_AGENT_GET_SELECTED_SOCKET
        gsock = nice_agent_get_selected_socket(
                ice_data->agent, ice_data->stream_id, 1);
#   else
        nice_socket = (NiceSocket *)ice_data->sockptr;
        gsock = nice_socket->fileno;
#   endif
    g_object_get(G_OBJECT(gsock), "fd", &fd, NULL);
#else
        nice_socket = (NiceSocket *)ice_data->sockptr;
        fd = nice_socket->fileno;
#endif

    *sock_dup = dup_socket(fd);
    if (*sock_dup == -1)
        return ICE_FAILURE;

    return ICE_SUCCESS;
}


/*
 * Destroy the ice session - closes the socket, stops event thread, and frees
 * resources.
 *
 * After the session is destroyed, another socket can be created at the
 * negotiated address and port. Note that this should be done immediately, or
 * the firewall sessions could time out, and the application becomes
 * responsible for maintaining the session.
 */
void ice_destroy(struct icedata *ice_data) {
    if (ice_data->state_mutex) {
        g_mutex_free(ice_data->state_mutex);
        ice_data->state_mutex = NULL;
    }
    if (ice_data->gather_cv) {
        g_cond_free(ice_data->gather_cv);
        ice_data->gather_cv = NULL;
    }
    if (ice_data->negotiate_cv) {
        g_cond_free(ice_data->negotiate_cv);
        ice_data->negotiate_cv = NULL;
    }

    if (ice_data->gloop) {
        g_main_loop_quit(ice_data->gloop);
        g_main_loop_unref(ice_data->gloop);
        ice_data->gloop = NULL;
    }
    if (ice_data->gloopthread) {
        g_thread_join(ice_data->gloopthread);
        ice_data->gloopthread = NULL;
    }

    if (ice_data->agent) {
        g_object_unref(ice_data->agent);
        ice_data->agent = NULL;
    }
    if (ice_data->gcontext) {
        g_main_context_unref(ice_data->gcontext);
        ice_data->gcontext = NULL;
    }
}


static void *thread_mainloop(void *data) {
    GMainLoop *loop = (GMainLoop *)data;
    g_main_loop_run(loop);

    g_debug("thread_mainloop exit");
    return NULL;
}


static void cb_candidate_gathering_done(NiceAgent *agent, guint stream_id,
                                        gpointer data) {
    struct icedata *ice_data = (struct icedata *)data;

    g_debug("SIGNAL: candidate gathering done");

    g_mutex_lock(ice_data->state_mutex);
    ice_data->gather_done = TRUE;
    g_cond_signal(ice_data->gather_cv);
    g_mutex_unlock(ice_data->state_mutex);
}


static void cb_component_state_changed(NiceAgent *agent, guint stream_id,
                                       guint component_id, guint state,
                                       gpointer data) {
    struct icedata *ice_data = (struct icedata *)data;

    g_debug("SIGNAL: state changed %d %d %s[%d]\n",
            stream_id, component_id, get_state_name(state), state);

    if (state == NICE_COMPONENT_STATE_READY
    || state == NICE_COMPONENT_STATE_FAILED) {
        g_mutex_lock(ice_data->state_mutex);
        ice_data->negotiate_done = TRUE;
        g_cond_signal(ice_data->negotiate_cv);
        g_mutex_unlock(ice_data->state_mutex);
    }
}


static void cb_new_selected_pair(NiceAgent *agent, guint stream_id,
                                 guint component_id, gchar *lfoundation,
                                 gchar *rfoundation, gpointer data) {
    GSList *lcands, *rcands;
    NiceCandidate *local, *remote;
    struct icedata *ice_data = (struct icedata *)data;

    g_debug("SIGNAL: selected pair %s %s", lfoundation, rfoundation);

    lcands = nice_agent_get_local_candidates(agent, stream_id, component_id);
    rcands = nice_agent_get_remote_candidates(agent, stream_id, component_id);

    local = find_candidate(lcands, lfoundation);
    remote = find_candidate(rcands, rfoundation);

    if (local && remote) {
        ice_data->bind_addr = nice_address_dup(&local->base_addr);
        ice_data->remote_addr = nice_address_dup(&remote->addr);
#ifndef HAVE_NICE_AGENT_GET_SELECTED_SOCKET
        ice_data->sockptr = local->sockptr;
#endif

        ice_data->selected_pair_done = TRUE;
    }

    free_candidates(lcands);
    free_candidates(rcands);
}


static void cb_nice_recv(NiceAgent *agent, guint stream_id, guint component_id,
                         guint len, gchar *buf, gpointer data) {
    g_debug("CALLBACK: nice recv '%.*s'", len, buf);
}


/* 
   From RFC 5245 Section 4.3:

   The attribute carries the IP address,
   port, and transport protocol for the candidate, in addition to its
   properties that need to be signaled to the peer for ICE to work: the
   priority, foundation, and component ID.  The candidate attribute also
   carries information about the candidate that is useful for
   diagnostics and other functions: its type and related transport
   addresses.

   At minimum we must exchange ufrag/pass and for each candidate:
    addr:port, priority, foundation
   Protocol can be assumed to be UDP, and comp_id assumed to be 1.

   Return -1 on failure, total bytes written not including '\0' on success.
 */
static int snprint_cand(char *out, size_t outlen,
                        const NiceCandidate *cand) {
    gchar ipaddr[INET6_ADDRSTRLEN];
    int written;

    nice_address_to_string(&cand->addr, ipaddr);

    /* (foundation),(prio),(addr),(port),(type) */
    written = snprintf(out, outlen, "%s,%u,%s,%u,%s",
                       cand->foundation,
                       cand->priority,
                       ipaddr,
                       nice_address_get_port(&cand->addr),
                       get_cand_type_name(cand->type)
                       );
    return written;
}


static NiceCandidate *parse_candidate(char *scand, guint stream_id) {
    char foundation[33], ipaddr[46], type[7];
    int cnt, port;
    unsigned int prio;
    gboolean ok;
    NiceCandidate *rval = NULL, *out = NULL;
    NiceCandidateType ntype;

    /* (foundation),(prio),(addr),(port),(type) */
    cnt = sscanf(scand, "%32[^,],%u,%45[^,],%d,%6s", foundation, &prio, ipaddr,
                 &port, type);
    if (cnt != 5) {
        return out;
    }

    if (strcmp(type, "host")==0)
        ntype = NICE_CANDIDATE_TYPE_HOST;
    else if (strcmp(type, "srflx")==0)
        ntype = NICE_CANDIDATE_TYPE_SERVER_REFLEXIVE;
    else if (strcmp(type, "relay")==0)
        ntype = NICE_CANDIDATE_TYPE_RELAYED;
    else {
        goto end;
    }

    out = nice_candidate_new(ntype);
    out->component_id = 1;
    out->stream_id = stream_id;
    out->transport = NICE_CANDIDATE_TRANSPORT_UDP;
    strncpy(out->foundation, foundation, NICE_CANDIDATE_MAX_FOUNDATION);
    out->priority = prio;

    ok = nice_address_set_from_string(&out->addr, ipaddr);
    if (!ok || !nice_address_is_valid(&out->addr)) {
        g_message("failed to parse addr: %s", ipaddr);
        goto end;
    }

    nice_address_set_port(&out->addr, port);

    rval = out;

end:
    if (rval == NULL && out)
        nice_candidate_free(out);

    return rval;
}


static int nice_p_address_safe_copy(NiceAddress *naddr, struct sockaddr *saddr,
                                    socklen_t *addrlen) {
    socklen_t requiredlen;
    switch (naddr->s.addr.sa_family) {
    case AF_INET:
        requiredlen = sizeof(struct sockaddr_in);
        break;
    case AF_INET6:
        requiredlen = sizeof(struct sockaddr_in6);
        break;
    default:
        g_error("Unknown address family: %u", naddr->s.addr.sa_family);
    }

    if (*addrlen < requiredlen) {
        g_message("sockaddr is too small to fit address: %u < %u",
                  *addrlen, requiredlen);
        return ICE_FAILURE;
    }

    *addrlen = requiredlen;

    nice_address_copy_to_sockaddr(naddr, saddr);
    return ICE_SUCCESS;
}


static NiceCandidate *find_candidate(GSList *candidates,
                                     const gchar *foundation) {
    GSList *item;
    NiceCandidate *c;

    for (item = candidates; item; item = item->next) {
        c = (NiceCandidate *)item->data;
        if (strncmp(c->foundation, foundation, sizeof(c->foundation))
        == 0) {
            return c;
        }
    }

    return NULL;
}


static void free_candidates(GSList *candidates) {
    GSList *item;

    for (item = candidates; item; item = item->next)
        nice_candidate_free((NiceCandidate *)item->data);
    g_slist_free(candidates);
}


static const char *get_cand_type_name(NiceCandidateType nct) {
    switch(nct) {
    case NICE_CANDIDATE_TYPE_HOST:
        return "host";
    case NICE_CANDIDATE_TYPE_SERVER_REFLEXIVE:
        return "srflx";
    case NICE_CANDIDATE_TYPE_PEER_REFLEXIVE:
        return "prflx";
    case NICE_CANDIDATE_TYPE_RELAYED:
        return "relay";
    }
    return "UNKNOWN";
}


static const char *get_state_name(guint state) {
    switch(state) {
    case NICE_COMPONENT_STATE_DISCONNECTED:
        return "disconnected";
    case NICE_COMPONENT_STATE_GATHERING:
        return "gathering";
    case NICE_COMPONENT_STATE_CONNECTING:
        return "connecting";
    case NICE_COMPONENT_STATE_CONNECTED:
        return "connected";
    case NICE_COMPONENT_STATE_READY:
        return "ready";
    case NICE_COMPONENT_STATE_FAILED:
        return "failed";
    }
    return "UNKNOWN";
}

#define ICE_MAX_ARGS 20
char **ice_parse_args(char *line, int *argc) {
    char * p = line;
    char ** parse_argv;
    parse_argv = calloc(ICE_MAX_ARGS, sizeof(char *));
    *argc = 0;
    while (*p != '\0' && *argc < ICE_MAX_ARGS) {
        parse_argv[*argc] = p;
        (*argc)++;
        p = strchr(p, ' ');
        if (p == NULL)
            break;
        *p = '\0';
        do { p++; } while (*p == ' ');
    }
    return parse_argv;
}

/*
 * Duplicate socket on win32 or POSIX. Returns -1 on error.
 */
static int dup_socket(int sock) {
#ifdef _WIN32
    int rval;
    WSAPROTOCOL_INFO protInfo;
    SOCKET new_sock;

    rval = WSADuplicateSocket(sock, GetCurrentProcessId(), &protInfo);
    if (rval == SOCKET_ERROR)
        return -1;

    new_sock = WSASocket(
        FROM_PROTOCOL_INFO, FROM_PROTOCOL_INFO, FROM_PROTOCOL_INFO,
        &protInfo, 0, WSA_FLAG_OVERLAPPED);
    if (new_sock == INVALID_SOCKET)
        return -1;
    return new_sock;
#else
    return dup(sock);
#endif
}
