/*
 * myproxy_common.h
 *
 * Internal header file that includes all headers needed for building
 * MyProxy in one place to ease porting.
 *
 */

#ifndef __MYPROXY_COMMON_H
#define __MYPROXY_COMMON_H

#include <globus_common.h> /* need to start w/ this to avoid later trouble */

#include <assert.h>
#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <netdb.h>
#include <netinet/in.h>	/* Might be needed before <arpa/inet.h> */
#include <arpa/inet.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>
#include <sys/ioctl.h>
#include <sys/param.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>
#include <dlfcn.h>

#if defined(HAVE_STDINT_H)
#include <stdint.h>
#else /* defined(HAVE_STDINT_H) */
#if !defined(SIZE_MAX)
#define SIZE_MAX ((size_t)-1)
#endif /* !defined(SIZE_MAX) */
#endif /* defined(HAVE_STDINT_H) */

#if defined(HAVE_GETOPT_H)
#include <getopt.h>
#endif

#if !defined(HAVE_SOCKLEN_T)
typedef int socklen_t;
#endif

#include <globus_gss_assist.h>
#include <globus_gsi_system_config.h>
#include <gssapi.h>
#include <openssl/bio.h>
#include <openssl/ui.h>

#include "myproxy.h" /* public headers */
#include "myproxy_extensions.h"
#include "myproxy_popen.h"
#include "myproxy_ocsp.h"
#include "accept_credmap.h"
#include "certauth_extensions.h"
#include "certauth_resolveuser.h"
#include "gsi_socket.h"
#include "port_getopt.h"
#include "safe_id_range_list.h"
#include "safe_is_path_trusted.h"
#include "ssl_utils.h"
#include "string_funcs.h"
#include "vparse.h"

#if defined(HAVE_LIBSASL2)
#include <sasl.h>
#include <saslutil.h>
#define SASL_BUFFER_SIZE 20480
#endif

#if defined(HAVE_LIBKRB5)
#include <krb5.h>
#endif

#if defined(HAVE_VOMS)
#include <voms_apic.h>
#include <newformat.h>
#include "vomsclient.h"
#endif

#include  "voms_utils.h"

#if defined(HAVE_SECURITY_PAM_APPL_H)
# include <security/pam_appl.h>
#elif defined(HAVE_PAM_PAM_APPL_H)
# include <pam/pam_appl.h>
#endif

#ifndef va_copy
#define va_copy(a,b) ((a) = (b))
#endif

#if defined(HAVE_PIDFILE_DECL)
#include "libutil.h"
#else
struct pidfh;
struct pidfh *pidfile_open(const char *path, mode_t mode, pid_t *pidptr);
int pidfile_write(struct pidfh *pfh);
int pidfile_close(struct pidfh *pfh);
int pidfile_remove(struct pidfh *pfh);
#endif

#endif /* __MYPROXY_COMMON_H */
