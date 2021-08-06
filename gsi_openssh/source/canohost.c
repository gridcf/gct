/* $OpenBSD: canohost.c,v 1.75 2020/10/18 11:32:01 djm Exp $ */
/*
 * Author: Tatu Ylonen <ylo@cs.hut.fi>
 * Copyright (c) 1995 Tatu Ylonen <ylo@cs.hut.fi>, Espoo, Finland
 *                    All rights reserved
 * Functions for returning the canonical host name of the remote site.
 *
 * As far as I am concerned, the code I have written for this software
 * can be used freely for any purpose.  Any derived versions of this
 * software must be clearly marked as such, and if the derived work is
 * incompatible with the protocol description in the RFC file, it must be
 * called by a name other than "ssh" or "Secure Shell".
 */

#include "includes.h"

#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/param.h>          /* for MAXHOSTNAMELEN */

#include <netinet/in.h>
#include <arpa/inet.h>

#include <errno.h>
#include <netdb.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <unistd.h>

#include "xmalloc.h"
#include "packet.h"
#include "log.h"
#include "canohost.h"
#include "misc.h"

/*
 * Returns the remote DNS hostname as a string. The returned string must not
 * be freed. NB. this will usually trigger a DNS query the first time it is
 * called.
 * This function does additional checks on the hostname to mitigate some
 * attacks on legacy rhosts-style authentication.
 * XXX is RhostsRSAAuthentication vulnerable to these?
 * XXX Can we remove these checks? (or if not, remove RhostsRSAAuthentication?)
 */

char *
remote_hostname(struct ssh *ssh)
{
	struct sockaddr_storage from;
	socklen_t fromlen;
	struct addrinfo hints, *ai, *aitop;
	char name[NI_MAXHOST], ntop2[NI_MAXHOST];
	const char *ntop = ssh_remote_ipaddr(ssh);

	/* Get IP address of client. */
	fromlen = sizeof(from);
	memset(&from, 0, sizeof(from));
	if (getpeername(ssh_packet_get_connection_in(ssh),
	    (struct sockaddr *)&from, &fromlen) == -1) {
		debug("getpeername failed: %.100s", strerror(errno));
		return xstrdup(ntop);
	}

	ipv64_normalise_mapped(&from, &fromlen);
	if (from.ss_family == AF_INET6)
		fromlen = sizeof(struct sockaddr_in6);

	debug3("Trying to reverse map address %.100s.", ntop);
	/* Map the IP address to a host name. */
	if (getnameinfo((struct sockaddr *)&from, fromlen, name, sizeof(name),
	    NULL, 0, NI_NAMEREQD) != 0) {
		/* Host name not found.  Use ip address. */
		return xstrdup(ntop);
	}

	/*
	 * if reverse lookup result looks like a numeric hostname,
	 * someone is trying to trick us by PTR record like following:
	 *	1.1.1.10.in-addr.arpa.	IN PTR	2.3.4.5
	 */
	memset(&hints, 0, sizeof(hints));
	hints.ai_socktype = SOCK_DGRAM;	/*dummy*/
	hints.ai_flags = AI_NUMERICHOST;
	if (getaddrinfo(name, NULL, &hints, &ai) == 0) {
		logit("Nasty PTR record \"%s\" is set up for %s, ignoring",
		    name, ntop);
		freeaddrinfo(ai);
		return xstrdup(ntop);
	}

	/* Names are stored in lowercase. */
	lowercase(name);

	/*
	 * Map it back to an IP address and check that the given
	 * address actually is an address of this host.  This is
	 * necessary because anyone with access to a name server can
	 * define arbitrary names for an IP address. Mapping from
	 * name to IP address can be trusted better (but can still be
	 * fooled if the intruder has access to the name server of
	 * the domain).
	 */
	memset(&hints, 0, sizeof(hints));
	hints.ai_family = from.ss_family;
	hints.ai_socktype = SOCK_STREAM;
	if (getaddrinfo(name, NULL, &hints, &aitop) != 0) {
		logit("reverse mapping checking getaddrinfo for %.700s "
		    "[%s] failed.", name, ntop);
		return xstrdup(ntop);
	}
	/* Look for the address from the list of addresses. */
	for (ai = aitop; ai; ai = ai->ai_next) {
		if (getnameinfo(ai->ai_addr, ai->ai_addrlen, ntop2,
		    sizeof(ntop2), NULL, 0, NI_NUMERICHOST) == 0 &&
		    (strcmp(ntop, ntop2) == 0))
				break;
	}
	freeaddrinfo(aitop);
	/* If we reached the end of the list, the address was not there. */
	if (ai == NULL) {
		/* Address not found for the host name. */
		logit("Address %.100s maps to %.600s, but this does not "
		    "map back to the address.", ntop, name);
		return xstrdup(ntop);
	}
	return xstrdup(name);
}

void
ipv64_normalise_mapped(struct sockaddr_storage *addr, socklen_t *len)
{
	struct sockaddr_in6 *a6 = (struct sockaddr_in6 *)addr;
	struct sockaddr_in *a4 = (struct sockaddr_in *)addr;
	struct in_addr inaddr;
	u_int16_t port;

	if (addr->ss_family != AF_INET6 ||
	    !IN6_IS_ADDR_V4MAPPED(&a6->sin6_addr))
		return;

	debug3("Normalising mapped IPv4 in IPv6 address");

	memcpy(&inaddr, ((char *)&a6->sin6_addr) + 12, sizeof(inaddr));
	port = a6->sin6_port;

	memset(a4, 0, sizeof(*a4));

	a4->sin_family = AF_INET;
	*len = sizeof(*a4);
	memcpy(&a4->sin_addr, &inaddr, sizeof(inaddr));
	a4->sin_port = port;
}

/*
 * Returns the local/remote IP-address/hostname of socket as a string.
 * The returned string must be freed.
 */
static char *
get_socket_address(int sock, int remote, int flags)
{
	struct sockaddr_storage addr;
	socklen_t addrlen;
	char ntop[NI_MAXHOST];
	int r;

	/* Get IP address of client. */
	addrlen = sizeof(addr);
	memset(&addr, 0, sizeof(addr));

	if (remote) {
		if (getpeername(sock, (struct sockaddr *)&addr, &addrlen) != 0)
			return NULL;
	} else {
		if (getsockname(sock, (struct sockaddr *)&addr, &addrlen) != 0)
			return NULL;
	}

	/* Work around Linux IPv6 weirdness */
	if (addr.ss_family == AF_INET6) {
		addrlen = sizeof(struct sockaddr_in6);
		ipv64_normalise_mapped(&addr, &addrlen);
	}

	switch (addr.ss_family) {
	case AF_INET:
	case AF_INET6:
		/* Get the address in ascii. */
		if ((r = getnameinfo((struct sockaddr *)&addr, addrlen, ntop,
		    sizeof(ntop), NULL, 0, flags)) != 0) {
			error_f("getnameinfo %d failed: %s",
			    flags, ssh_gai_strerror(r));
			return NULL;
		}
		return xstrdup(ntop);
	case AF_UNIX:
		/* Get the Unix domain socket path. */
		return xstrdup(((struct sockaddr_un *)&addr)->sun_path);
	default:
		/* We can't look up remote Unix domain sockets. */
		return NULL;
	}
}

char *
get_peer_ipaddr(int sock)
{
	char *p;

	if ((p = get_socket_address(sock, 1, NI_NUMERICHOST)) != NULL)
		return p;
	return xstrdup("UNKNOWN");
}

char *
get_local_ipaddr(int sock)
{
	char *p;

	if ((p = get_socket_address(sock, 0, NI_NUMERICHOST)) != NULL)
		return p;
	return xstrdup("UNKNOWN");
}

char *
get_local_name(int fd)
{
	char *host, myname[NI_MAXHOST];

	/* Assume we were passed a socket */
	if ((host = get_socket_address(fd, 0, NI_NAMEREQD)) != NULL)
		return host;

	/* Handle the case where we were passed a pipe */
	if (gethostname(myname, sizeof(myname)) == -1) {
		verbose_f("gethostname: %s", strerror(errno));
		host = xstrdup("UNKNOWN");
	} else {
		host = xstrdup(myname);
	}

	return host;
}

/* Returns the local/remote port for the socket. */

static int
get_sock_port(int sock, int local)
{
	struct sockaddr_storage from;
	socklen_t fromlen;
	char strport[NI_MAXSERV];
	int r;

	/* Get IP address of client. */
	fromlen = sizeof(from);
	memset(&from, 0, sizeof(from));
	if (local) {
		if (getsockname(sock, (struct sockaddr *)&from, &fromlen) == -1) {
			error("getsockname failed: %.100s", strerror(errno));
			return 0;
		}
	} else {
		if (getpeername(sock, (struct sockaddr *)&from, &fromlen) == -1) {
			debug("getpeername failed: %.100s", strerror(errno));
			return -1;
		}
	}

	/* Work around Linux IPv6 weirdness */
	if (from.ss_family == AF_INET6)
		fromlen = sizeof(struct sockaddr_in6);

	/* Non-inet sockets don't have a port number. */
	if (from.ss_family != AF_INET && from.ss_family != AF_INET6)
		return 0;

	/* Return port number. */
	if ((r = getnameinfo((struct sockaddr *)&from, fromlen, NULL, 0,
	    strport, sizeof(strport), NI_NUMERICSERV)) != 0)
		fatal_f("getnameinfo NI_NUMERICSERV failed: %s",
		    ssh_gai_strerror(r));
	return atoi(strport);
}

int
get_peer_port(int sock)
{
	return get_sock_port(sock, 0);
}

int
get_local_port(int sock)
{
	return get_sock_port(sock, 1);
}

void
resolve_localhost(char **host)
{
	struct hostent *hostinfo;

	hostinfo = gethostbyname(*host);
	if (hostinfo == NULL || hostinfo->h_name == NULL) {
		debug("gethostbyname(%s) failed", *host);
		return;
	}
	if (hostinfo->h_addrtype == AF_INET) {
		struct in_addr addr;
		addr = *(struct in_addr *)(hostinfo->h_addr);
		if (ntohl(addr.s_addr) == INADDR_LOOPBACK) {
			char buf[MAXHOSTNAMELEN];
			if (gethostname(buf, sizeof(buf)) < 0) {
				debug("gethostname() failed");
				return;
			}
			hostinfo = gethostbyname(buf);
			free(*host);
			if (hostinfo == NULL || hostinfo->h_name == NULL) {
				*host = xstrdup(buf);
			} else {
				*host = xstrdup(hostinfo->h_name);
			}
		}
	}
}
