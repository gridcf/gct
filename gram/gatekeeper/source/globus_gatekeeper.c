/*
 * Copyright 1999-2010 University of Chicago
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

/**
 * @file globus_gatekeeper.c
 * @brief GRAM Gatekeeper
 *
 * This source file has been modified by Brent Milne (BMilne@lbl.gov)
 * with extensions for UNICOS.
 * September 1998
 */

/* Include header files */
#if defined(_AIX32) && !defined(_ALL_SOURCE)
#define _ALL_SOURCE
#endif
#include "globus_config.h"
#include "globus_gatekeeper_config.h"

#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <fcntl.h>
#include <pwd.h>
#include <sys/param.h>
#include <unistd.h>
#include <errno.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/uio.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <syslog.h>
#include <netdb.h>
#include <netinet/in.h>

#include <time.h>
#include <sys/stat.h>
#include <sys/file.h>
#include <sys/ioctl.h>
#include <sys/signal.h>
#include <sys/wait.h>
#include <regex.h>


#include "globus_common.h"
#include "globus_gss_assist.h"
#include "gssapi.h"

#if defined(TARGET_ARCH_SOLARIS)
#include <termios.h>
#endif

#if defined(HAVE_SOCKLEN_T)
#define netlen_t socklen_t
#elif defined(TARGET_ARCH_AIX)
#define netlen_t size_t
#else
#define netlen_t int
#endif

#if HAVE_STRINGS_H
#include <strings.h>
#endif

#if HAVE_STRING_H
#include <string.h>
#endif

#include "globus_gatekeeper_utils.h"
#include "globus_gsi_system_config.h"

#include "openssl/bio.h"
#include "openssl/pem.h"

#if OPENSSL_VERSION_NUMBER < 0x0090801fL
#define GT_D2I_DATA_CAST (unsigned char **)
#else
#define GT_D2I_DATA_CAST 
#endif


char *pidpath = NULL;
static gss_OID_desc gss_ext_x509_cert_chain_oid_desc =
     {11, "\x2b\x06\x01\x04\x01\x9b\x50\x01\x01\x01\x08"}; 
static gss_OID_desc * gss_ext_x509_cert_chain_oid =
                &gss_ext_x509_cert_chain_oid_desc;
/******************************************************************************
                               Type definitions
******************************************************************************/

#define SERVICE_ARGS_MAX 100
/* offsets in the service command line after tokenize */
/* note the one before the path gets overlaid */
#define SERVICE_OPTIONS_INDEX 0
#define SERVICE_USER_INDEX 1
#define SERVICE_PATH_INDEX 2
#define SERVICE_ARG0_INDEX 3
#define SERVICE_ARG1_INDEX 4

#define SERVICE_OPTIONS_MAX 20
/******************************************************************************
                          Module specific prototypes
******************************************************************************/
static void doit(void);
static int logging_startup(void);
static int logging_phase2(void);
static void failure(int failure_type, char *s);
static void notice(int, char *s);
static int net_accept(int socket);
static void net_setup_listener(int backlog, int *port, int *socket);
static void error_check(int val, char *string);
static char *timestamp(void);

static void
read_header_and_body(
    gss_ctx_id_t                        context,
    char **                             header_out,
    char **                             body_out,
    size_t *                            bodylen_out);

static char * genfilename(char * prefix, char * path, char * sufix);

/*
 * GSSAPI - credential handle for this process
 */
static gss_cred_id_t credential_handle = GSS_C_NO_CREDENTIAL;
static gss_cred_id_t delegated_cred_handle = GSS_C_NO_CREDENTIAL;
static gss_ctx_id_t  context_handle    = GSS_C_NO_CONTEXT;

/******************************************************************************
                       Define module specific variables
******************************************************************************/
#define MAXARGS 256
#define DEFAULT_SERVICENAME "gsigatekeeper"
#define DEFAULT_PORT 2119
#define MAX_MESSAGE_LENGTH 100000
#ifndef GRAM_K5_EXE
#       define GRAM_K5_EXE "globus-k5"
#endif
#ifndef GLOBUS_GATEKEEPER_HOME
#       define GLOBUS_GATEKEEPER_HOME "/etc"
#endif
#ifndef LOGFILE
#define LOGFILE ""
#endif

enum
{
    FAILED_AUTHORIZATION = 1,
    FAILED_SERVICELOOKUP,
    FAILED_SERVER,
    FAILED_NOLOGIN,
    FAILED_AUTHENTICATION,
    FAILED_PING,
    FAILED_TOOLARGE
};


static char     tmpbuf[1024];
#define notice2(i,a,b) {sprintf(tmpbuf, a,b); notice(i,tmpbuf);}
#define notice3(i,a,b,c) {sprintf(tmpbuf, a,b,c); notice(i,tmpbuf);}
#define notice4(i,a,b,c,d) {sprintf(tmpbuf, a,b,c,d); notice(i,tmpbuf);}
#define notice5(i,a,b,c,d,e) {sprintf(tmpbuf, a,b,c,d,e); notice(i,tmpbuf);}
#define failure2(t,a,b) {sprintf(tmpbuf, a,b); failure(t,tmpbuf);}
#define failure3(t,a,b,c) {sprintf(tmpbuf, a,b,c); failure(t,tmpbuf);}
#define failure4(t,a,b,c,d) {sprintf(tmpbuf, a,b,c,d); failure(t,tmpbuf);}

enum gatekeeper_launch_method
{
    FORK_AND_EXIT = 1,
    FORK_AND_WAIT = 2,
    DONT_FORK = 3,
    FORK_AND_PROXY = 4
};

static enum gatekeeper_launch_method launch_method = FORK_AND_EXIT;

extern int      errno;

static int      connection_fd;
static int      listener_fd = -1;

static FILE *   usrlog_fp;
static char *   logfile = LOGFILE;
static char *   acctfile;
static volatile int	logrotate;
static pid_t    gatekeeper_pid;
static unsigned reqnr;
static int      gatekeeper_test;
static int      gatekeeper_uid;
static int      daemon_port;
static int      log_facility = LOG_DAEMON;
static int      logging_syslog;
static int      logging_usrlog;
static int      debug;
static int      foreground;
static int      krb5flag;
static int      run_from_inetd;
static char *   gatekeeperhome = NULL;
static char *   libexecdir = NULL;
static char *   logdir = NULL;
static char *   service_name = NULL;
static char *   grid_services = "${sysconfdir}/grid-services";
static char *   gridmap = NULL;
static char *   globuskmap = "/etc/globuskmap";
static char *   globusnologin ="globus-nologin";
static char *   x509_cert_dir = NULL;
static char *   x509_user_proxy = NULL;
static char *   x509_user_cert = NULL;
static char *   x509_user_key = NULL;
static char *   desired_name_char = NULL;
static int      ok_to_send_errmsg = 0;
static FILE *   fdout;
static int      got_ping_request = 0;

/******************************************************************************
Function:       get_globusid()
Description:    Get the globusid from gssapi or environment if possible.
Parameters:
Returns:
******************************************************************************/
static char * 
get_globusid()
{
    char *            globusid;
    char *            globusid_tmp;
    gss_name_t        server_name = GSS_C_NO_NAME;
    gss_buffer_desc   server_buffer_desc = GSS_C_EMPTY_BUFFER;
    gss_buffer_t      server_buffer = &server_buffer_desc; 
    OM_uint32         major_status = 0;
    OM_uint32         minor_status = 0;
    OM_uint32         minor_status2 = 0;

    if ((major_status = gss_inquire_cred(&minor_status,
                                         credential_handle,
                                         &server_name,
                                         NULL,
                                         NULL,
                                         NULL)) == GSS_S_COMPLETE)
    {
        major_status = gss_display_name(&minor_status,
                                        server_name,
                                        server_buffer,
                                        NULL);
        gss_release_name(&minor_status2, &server_name);
    }
    /*
     * The gssapi_cleartext does not implement gss_inquire_cred,
     * so fall back to using environment variable.
     */
    if (major_status == GSS_S_COMPLETE) 
    {
        globusid = server_buffer_desc.value;
    }
    else 
    {
        globusid = getenv("GLOBUSID");
        globusid = (globusid ? globusid : "GLOBUSID");
    }
    globusid_tmp = strdup(globusid);

    if (server_buffer_desc.value)
    {
        gss_release_buffer(&minor_status2, server_buffer);
    }
    return globusid_tmp;
}

/******************************************************************************
Function:       terminate()
Description:    Handle a  SIGTERM, and specificly close the listener_fd
                                This should avoid problems when the gatekeeper
                                is shutdown. 
Parameters:
Returns:
******************************************************************************/
void 
terminate(int s)
{
    notice2(LOG_NOTICE,"Gatekeeper received signal:%d",s);
    if (listener_fd > 0)
    {
        if (close(listener_fd) == -1)
        {
            notice3(LOG_ERR, "Shutdown of %d: %.100s",
                    listener_fd, strerror(errno));
                                        
        }
        listener_fd = -1;
    }
    if (pidpath)
    {
        remove(pidpath);
    }

    failure2(FAILED_SERVER,"Gatekeeper shutdown on signal:%d",s)
}

/******************************************************************************
Function:       rotatelog()
Description:    Handle a SIGUSR1: set a flag indicating the logfile should be
                                  rotated by the main loop.
Parameters:
Returns:
******************************************************************************/
static void
rotatelog(int s)
{
    logrotate = 1;
}

/******************************************************************************
Function:       new_acct_file()
Description:    Rotate old and open new job accounting file.
Parameters:
Returns:
******************************************************************************/
static void
new_acct_file(void)
{
    static int acct_fd = -1;

    if (acct_fd >= 0)
    {
	if (strcmp(acctfile, logfile) != 0)
	{
	    static int seqnr;
            char *acctpath = genfilename(logdir, acctfile, NULL);
	    char *oldpath = malloc(strlen(acctpath) + 64);
	    time_t clock = time((time_t *) 0);
	    struct tm *tmp = localtime(&clock);
	    int ret;

	    sprintf(oldpath, "%s.%04d%02d%02d%02d%02d%02d.%d", acctpath,
		tmp->tm_year + 1900, tmp->tm_mon + 1, tmp->tm_mday,
		tmp->tm_hour, tmp->tm_min, tmp->tm_sec, seqnr++);

	    if ((ret = rename(acctpath, oldpath)) != 0)
	    {
		notice4(LOG_ERR, "ERROR: cannot rename %s to %s: %s",
		    acctpath, oldpath, strerror(errno));
	    }
	    else
	    {
		notice2(0, "renamed accounting file %s", oldpath);
	    }

	    free(acctpath);
	    free(oldpath);

	    if (ret) {
		return;
	    }
	}

	close(acct_fd);
	acct_fd = -1;
    }

    if (!acctfile)
    {
	acctfile = logfile;
    }

    if (acctfile && *acctfile)
    {
	const char *acct_fd_var = "GATEKEEPER_ACCT_FD";
	char *acctpath = genfilename(logdir, acctfile, NULL);

	acct_fd = open(acctpath, O_WRONLY | O_APPEND | O_CREAT, 0644);

	if (acct_fd < 0)
	{
	    notice3(LOG_ERR, "ERROR: cannot open accounting file '%s': %s",
		acctpath, strerror(errno));

	    globus_libc_unsetenv(acct_fd_var);
	}
	else
	{
	    /*
	     * Now inform JM via environment.
	     */

	    char buf[32];

	    sprintf(buf, "%d", acct_fd);

	    globus_libc_setenv(acct_fd_var, buf, 1);

	    notice4(0, "%s=%s (%s)", acct_fd_var, buf, acctpath);
	}

	free(acctpath);
    }
}

/******************************************************************************
Function:       genfilename()
Description:    generate an absolute file name given a starting prefix,
                a relative or absolute path, and a sufix
                Only use prefix if path is relative.
Parameters:
Returns:                a pointer to a string which could be freeded.
******************************************************************************/

static char *
genfilename(char * prefixp, char * pathp, char * sufixp)
{
    char * newfilename;
    int    prefixl, pathl, sufixl;
    char * prefix,  * path, * sufix;
    char * evaluated_path;
    globus_result_t result;

    prefix = (prefixp) ? prefixp : "";
    path   = (pathp) ? pathp : "";
    sufix  = (sufixp) ? sufixp : "";

    prefixl = strlen(prefix);
    sufixl  =  strlen(sufix); 

    result = globus_eval_path(path, &evaluated_path);
    if (result != GLOBUS_SUCCESS)
    {
        failure2(FAILED_SERVER, "evaluating path %s failed\n", path);
    }
    pathl   =  strlen(evaluated_path);

    newfilename = (char *) calloc(1, (prefixl + pathl + sufixl + 3));
    if (newfilename) 
    {
        if (*evaluated_path != '/')
        {
            strcat(newfilename, prefix);
            if ((prefixl != 0) && (prefix[prefixl-1] != '/'))
            {
                strcat(newfilename, "/");
            }
        }
        strcat(newfilename, evaluated_path);
        if ((pathl  != 0) &&
            (sufixl != 0) && 
            (evaluated_path[pathl-1] != '/') && 
            sufix[0] != '/')
        {
            strcat(newfilename, "/");
        }
        strcat(newfilename, sufix);
    }
    return newfilename;
}

/******************************************************************************
Function:       main()
Description:    
Parameters:
Returns:
******************************************************************************/
int 
main(int xargc,
     char ** xargv)
{
    int argc = xargc;
    char ** argv = xargv;
    int    i;
    int    fd;
    int    pid;
    int    ttyfd;
    int    rc;
    globus_socklen_t   namelen;
    globus_sockaddr_t name;

    /* GSSAPI status vaiables */
    OM_uint32 major_status = 0;
    OM_uint32 minor_status = 0;

    globus_thread_set_model(GLOBUS_THREAD_MODEL_NONE);
    rc = globus_module_activate(GLOBUS_GSI_GSS_ASSIST_MODULE);
    if(rc != GLOBUS_SUCCESS)
    {
        exit(1);
    }

    gatekeeper_pid = getpid();

    gatekeeper_uid = getuid();
    if (gatekeeper_uid == 0)
    {
        struct servent * servent;
        /*
         * If root, use standard service port
         */
        servent = getservbyname(DEFAULT_SERVICENAME, "tcp");

        if (servent == NULL)
        {
            daemon_port = DEFAULT_PORT;
        }
        else
        {
            daemon_port = servent->s_port;
        }
    }
    else
    {
        /*
         * If not root, let net_setup_listener call provide the port
         * (may be overridden by the "-p" option, see below)
         */
        daemon_port = 0;
    }

    /*
     * Decide if we are being run from inetd.
     * If so, then stdin will be connected to a socket,
     * so getpeername() will succeed.
     */

    namelen = sizeof(name);
    if (getpeername(0, (struct sockaddr *) &name, &namelen) < 0)
    {
        run_from_inetd = 0;
    }
    else
    {
        run_from_inetd = 1;
        /* 
         * can't have stdout pointing at socket, some of the
         * old-old gssapi code writes to stdout so point at stderr
         */
        close(1);
        (void) open("/dev/null",O_WRONLY);
    }

    /*
     * Parse the command line arguments
     */
    /* 
     * But first see if the first parameter is a -c or -conf filename
     * which has the real parameter list. This is needed
     * since the parameters are getting long, and inetd/wrapper
     * may have a problem.  Will assume there are at most 52 
     * args, in a buffer of BUFSIZ at the most. 
     */

    /* But before that, check if there is a -test option
     * after the -c or -conf file. This will test the gatekeeper
     * -test can also be in the options file 
     * this will run in forground, and not inetd
     */

    if (argc == 4 && !strcmp(argv[3],"-test"))
    {
        gatekeeper_test++;
        argc--;
    }

    if ( argc == 3 &&
         (!strcmp(argv[1],"-c") ||
          !strcmp(argv[1],"-conf"))
        )
    {
        char ** newargv;
        char * newbuf;  
        int newargc = 52;
        int  pfd;
          
        newargv = (char**) malloc(newargc * sizeof(char *)); /* not freeded */
        newbuf = (char *) malloc(BUFSIZ);  /* dont free */
        newargv[0] = argv[0];
        pfd = open(argv[2],O_RDONLY);
        i = read(pfd, newbuf, BUFSIZ-1);
        if (i < 0) 
            failure(FAILED_SERVER, "Unable to read extra parameters");
        newbuf[i] = '\0';
        close(pfd);

        newargv[0] = argv[0];
        newargc--;
        globus_gatekeeper_util_tokenize(newbuf, 
                                        &newargv[1],
                                        &newargc,
                                        " \t\n");
        argv = newargv;
        argc = newargc + 1;
    }

    for (i = 1; i < argc; i++)
    {
        if ((strcmp(argv[i], "-d") == 0) ||
            (strcmp(argv[i], "-debug") == 0 ))
        {
            debug = 1;
            foreground = 1;   /* Run in the forground */
        }
        else if (strcmp(argv[i], "-inetd") == 0)
        {
            if(!run_from_inetd)
            {
                if(!gatekeeper_test)
                {
                    fprintf(stderr, "Gatekeeper running as daemon, ignoring -inetd!\n");
                }
            }
            else
            {
                run_from_inetd = 1;
                foreground = 0;
            }
            
        }
        else if (((strcmp(argv[i], "-p") == 0) ||
                  (strcmp(argv[i], "-port") == 0))
                 && (i + 1 < argc))
        {
            daemon_port = atoi(argv[i+1]);
            i++;
        }
        else if (((strcmp(argv[i], "-l") == 0) ||
                  (strcmp(argv[i], "-logfile") == 0))
                 && (i + 1 < argc))
        {
            logfile =  argv[i+1];
            i++;
        }
        else if ((strcmp(argv[i], "-lf") == 0) && (i + 1 < argc))
        {
            char * facilitystring = argv[++i];
            int fi;

            struct facmap {
                char * facility_name;
                int facility;
            } facilities[] = {
                /* POSIX.1 facilities */
                {"LOG_KERN", LOG_KERN},
                {"LOG_USER", LOG_USER},
                {"LOG_MAIL", LOG_MAIL},
                {"LOG_NEWS", LOG_NEWS},
                {"LOG_UUCP", LOG_UUCP},
                {"LOG_DAEMON", LOG_DAEMON},
                {"LOG_AUTH", LOG_AUTH},
                {"LOG_CRON", LOG_CRON},
                {"LOG_LPR", LOG_LPR},
                {"LOG_LOCAL0", LOG_LOCAL0},
                {"LOG_LOCAL1", LOG_LOCAL1},
                {"LOG_LOCAL2", LOG_LOCAL2},
                {"LOG_LOCAL3", LOG_LOCAL3},
                {"LOG_LOCAL4", LOG_LOCAL4},
                {"LOG_LOCAL5", LOG_LOCAL5},
                {"LOG_LOCAL6", LOG_LOCAL6},
                {"LOG_LOCAL7", LOG_LOCAL7},
                {NULL, 0}
            };

            for (fi = 0; facilities[fi].facility_name != NULL; fi++)
            {
                if (strcmp(facilitystring, facilities[fi].facility_name) == 0)
                {
                    log_facility = facilities[fi].facility;
                    break;
                }
            }

            if (facilities[fi].facility_name == NULL)
            {
                if (isdigit(facilitystring[0]))
                {
                    log_facility = atoi(facilitystring);
                }
            }
        }
        else if ((strcmp(argv[i], "-acctfile") == 0)
                 && (i + 1 < argc))
        {
            acctfile = argv[i+1];
            i++;
        }
        else if ((strcmp(argv[i], "-home") == 0)
                 && (i + 1 < argc))
        {
            /* Also known as $GLOBUS_LOCATION */
            gatekeeperhome = argv[i+1];
            i++;
        }
        else if ((strcmp(argv[i], "-e") == 0)
                 && (i + 1 < argc))
        {
            libexecdir = argv[i+1];
            i++;
        }
        else if ((strcmp(argv[i], "-grid_services") == 0)
                 && (i + 1 < argc))
        {
            grid_services = argv[i+1];
            i++;
        }
        else if (strcmp(argv[i], "-test") == 0)
        {
            gatekeeper_test++;
        }
                
        else if ((strcmp(argv[i], "-globusid") == 0)
                 && (i + 1 < argc))
        {
            globus_libc_setenv("GLOBUSID", argv[i+1],1);
            i++;
        }
        else if ((strcmp(argv[i], "-gridmap") == 0)
                 && (i + 1 < argc))
        {
            gridmap = argv[i+1];
            i++;
        }
        
        /* set environment variables used by gssapi_ssleay */

        else if ((strcmp(argv[i], "-x509_cert_dir") == 0)
                 && (i + 1 < argc))
        {
            x509_cert_dir = argv[i+1];
            i++;
        }
        else if ((strcmp(argv[i], "-x509_user_proxy") == 0)
                 && (i + 1 < argc))
        {
            x509_user_proxy = argv[i+1];
            i++;
        }
        else if ((strcmp(argv[i], "-x509_user_cert") == 0)
                 && (i + 1 < argc))
        {
            x509_user_cert = argv[i+1];
            i++;
        }
        else if ((strcmp(argv[i], "-x509_user_key") == 0)
                 && (i + 1 < argc))
        {
            x509_user_key = argv[i+1];
            i++;
        }
        
        else if ((strcmp(argv[i], "-desired_name") == 0)
                 && (i + 1 < argc))
        {
            desired_name_char = argv[i+1];
            i++;
        }

        else if ((strcmp(argv[i], "-globuskmap") == 0)
                 && (i + 1 < argc))
        {
            globuskmap = argv[i+1];
            krb5flag = 1;
            i++;
        }
        else if (strcmp(argv[i], "-k") == 0)
        {
            krb5flag = 1;
        }
        else if ((strcmp(argv[i], "-f") == 0) ||
                 (strcmp(argv[i], "-foreground") == 0))
        {
            if(!run_from_inetd)
            {
                /* make the daemon run in the Foreground */
                
                foreground = 1;
                run_from_inetd = 0;
            }
        }
	else if ((strcmp(argv[i], "-launch_method") == 0)
		 && (i + 1 < argc))
	{
	    if (strcmp(argv[i + 1], "fork_and_exit") == 0)
	    {
		launch_method = FORK_AND_EXIT;
	    }
            else if (strcmp(argv[i + 1], "fork_and_proxy") == 0)
            {
                launch_method = FORK_AND_PROXY;
            }
	    else if(!run_from_inetd)
	    {
		fprintf(stderr, "Gatekeeper running as daemon, "
			"ignoring -launch_method!\n");
	    }
	    else if (strcmp(argv[i + 1], "fork_and_wait") == 0)
	    {
		launch_method = FORK_AND_WAIT;
	    }
	    else if (strcmp(argv[i + 1], "dont_fork") == 0)
	    {
		launch_method = DONT_FORK;
	    }
	    else
	    {
		fprintf(stderr, "Bad -launch_method argument %s\n",
			argv[i + 1]);
	    }
	    i++;
	}
	else if ((strcmp(argv[i], "-pidfile") == 0)
		 && (i + 1 < argc))
        {
            pidpath = argv[++i];
        }
        else
        {
            if (strcmp(argv[i], "-help") != 0)
            {
                fprintf(stderr, "Unknown argument %s\n", argv[i]);
            }
            fprintf(stderr, "Usage: %s\n%s\n",
                    argv[0], 
                    "{-conf parmfile [-test]} | {[-d[ebug] [-inetd | -f] [-p[ort] port]\n"
                    "[-home path] [-l[ogfile] logfile] [-lf LOG-FACILITY] [-acctfile acctfile] [-e path]\n"
                    "[-launch_method fork_and_exit|fork_and_wait|dont_fork|fork_and_proxy]\n"
                    "[-grid_services file]\n"
                    "[-globusid globusid] [-gridmap file]\n"
                    "[-x509_cert_dir path]\n"
                    "[-x509_user_cert file] [-x509_user_key file]\n"
                    "[-x509_user_proxy file]\n"
                    "[-k] [-globuskmap file]\n"
                    "[-pidfile path]\n"
                    "}"
                );
            exit(1);
        }
    }

    if (gatekeeperhome)
    {
        globus_libc_setenv("GLOBUS_LOCATION", gatekeeperhome, 1);
        logdir = genfilename(gatekeeperhome, "var", NULL);
    }
    else if ((gatekeeperhome = getenv("GLOBUS_LOCATION")) != NULL)
    {
        logdir = genfilename(gatekeeperhome, "var", NULL);
    }
    else
    {
        gatekeeperhome = GLOBUS_LOCATION;
        logdir = GLOBUS_LOG_DIR;
    }

    if (libexecdir == NULL)
    {
        libexecdir = malloc(strlen(gatekeeperhome) + strlen("/libexec") + 1);
        sprintf(libexecdir, "%s/libexec", gatekeeperhome);
    }


    /*
     * Dont use default env proxy cert for gatekeeper if run as root
     * this might get left over. You can still use -x509_user_proxy
     */

    if (gatekeeper_uid == 0)
    {
        globus_libc_unsetenv("X509_USER_PROXY");
    }

    if (gatekeeper_test)
    {
        fprintf(stderr,"Testing gatekeeper\n");
        fprintf(stderr,"Local user id (uid)          : %d\n", gatekeeper_uid);
        fprintf(stderr,"GLOBUS_LOCATION directory    : %s\n", 
                gatekeeperhome ? gatekeeperhome : "(not defined)");
        fprintf(stderr,"Libexec directory        : %s\n",
                libexecdir ? libexecdir : "(not defined)");
        fprintf(stderr,"log directory            : %s\n",
                logdir ? logdir : "(not defined)");

        run_from_inetd = 0;
        foreground = 1;
    }

    if (gridmap != NULL)
    {
        globus_libc_setenv("GRIDMAP", gridmap, 1);
    }

    if (x509_cert_dir)
    {
        globus_libc_setenv("X509_CERT_DIR", x509_cert_dir, 1);
    }
    if (x509_user_proxy)
    {
        globus_libc_setenv("X509_USER_PROXY", x509_user_proxy, 1);
    }

    if (x509_user_cert)
    {
        globus_libc_setenv("X509_USER_CERT", x509_user_cert, 1);
    }
    if (x509_user_key)
    {
        globus_libc_setenv("X509_USER_KEY", x509_user_key, 1);
    }
    if (krb5flag) 
    {
        globus_libc_setenv("GLOBUSKMAP", globuskmap, 1);
    }

    if (run_from_inetd)
    {
        (void) close(2);  /* dont want messages. logging will use fd=2 */ 
        (void) open("/dev/null",O_WRONLY);
    }

    if (logging_startup() != 0)
    {
        failure(FAILED_SERVER, "Logging startup failure");
    }

    notice4(LOG_INFO, "%s pid=%d starting at %s",
            argv[0], getpid(), timestamp());

    /*
     * Setup SIGCHLD signal handler to reap processes that we create
     */
    {
        struct sigaction act;
        act.sa_handler = SIG_IGN;
        sigemptyset(&act.sa_mask);
        sigaddset(&act.sa_mask, SIGCHLD);
#ifdef SA_NOCLDWAIT
        act.sa_flags = SA_NOCLDWAIT;
#else
        /* This might leave zombies on non-POSIX systems like Hurd */
        act.sa_flags = 0;
#endif
        sigaction(SIGCHLD, &act, NULL);
        if (!run_from_inetd)
        {
            act.sa_handler = terminate;
            sigemptyset(&act.sa_mask);
            sigaddset(&act.sa_mask, SIGTERM);
            act.sa_flags = 0;
            sigaction(SIGTERM, &act, NULL);
        }
	act.sa_handler = rotatelog;
	sigemptyset(&act.sa_mask);
	sigaddset(&act.sa_mask, SIGUSR1);
	act.sa_flags = 0;
	sigaction(SIGUSR1, &act, NULL);
    }

    if (run_from_inetd)
    {
        logging_phase2();
        dup2(2,1); /* point stdout at log as well */
    }

    /*
     * Always make stdout unbuffered: otherwise the fclose(stdout)
     * in doit() will flush any buffered output again and again!
     */
    setbuf(stdout,NULL);

    /* Get the GSS credential for the accepter
     * If not run_from_inetd we can prompt here.
     * If we are running as a deamon, and should not
     * have any prompts
     */
    major_status = globus_gss_assist_acquire_cred_ext(&minor_status,
                                                      desired_name_char,
                                                      GSS_C_INDEFINITE,
                                                      GSS_C_NO_OID_SET,
                                                      GSS_C_ACCEPT,
                                                      &credential_handle,
                                                      NULL,
                                                      NULL);

    if (major_status != GSS_S_COMPLETE)
    {
        globus_gss_assist_display_status(stderr,
                                         "GSS failed getting server credentials: ",
                                         major_status,
                                         minor_status,
                                         0);

        failure(FAILED_SERVER, "GSS failed to get server credentials\n");
    }

    if (gatekeeper_test) 
    {
        fprintf(stderr,"Gatekeeper subject name  : \"%s\"\n",
                get_globusid());
        fprintf(stderr,"Gatekeeper test complete : Success!\n\n");
        fprintf(stderr,"Gatekeeper shutting down!\n");
        exit(0);
    }

    {
        char hostname[255] = "";
        char *globusid;
        struct hostent *hp;
        char *fqdn;
        char * contact_string;
      
        fqdn = getenv("GLOBUS_HOSTNAME");

        if (fqdn == NULL)
        {
            gethostname(hostname, sizeof(hostname)-1);
          
            if ((hp = gethostbyname(hostname)))
            {
                fqdn = (char *) hp->h_name;
            }
            else
            {
                fqdn = (hostname[0] != 0 ? hostname : "HOSTNAME");
            }
        }
      
        globusid = get_globusid();


        if (!run_from_inetd)
        {
            logging_phase2(); /* now set stderr to logfile after gss prompts */

            net_setup_listener(256, &daemon_port, &listener_fd);
        }
      
        /* ajr,vs --changed printf to sprintf, and added setenv
         * This is considered to be a temporary change */
        if ((contact_string = (char *)malloc(strlen(fqdn) 
                                             + strlen(globusid) + 40))) 
        {
        
            sprintf(contact_string, "%s:%d:%s",
                    fqdn, daemon_port, globusid);
            if (!run_from_inetd)
                printf("GRAM contact: %s\n", contact_string);
            globus_libc_setenv("GLOBUS_GATEKEEPER_CONTACT_STRING",
                   contact_string,
                   1);
     
            if (!run_from_inetd && strlen(contact_string)<sizeof(tmpbuf)-15)
                notice2(LOG_INFO, "GRAM contact: %s\n", contact_string);

            free(contact_string);
        }
        free(globusid);
    }

    new_acct_file();

    if (run_from_inetd)
    {
        (void) setsid();
        doit();
    }
    else
    {
        if (!foreground)
        {
            /*
             * Fork off a child, terminate the parent, and detach
             * the child from the tty.
             */
            if (fork())
                exit(0);

	    gatekeeper_pid = getpid();

            if (!logging_usrlog)
            {
                (void) close(2); /* close stderr as well */
		(void) open("/dev/null",O_WRONLY);
            }

            (void) close(0);
            (void) close(1);

#           if (defined(SYSV) || \
                defined(__hpux) || \
                defined(CRAY) || \
                defined(TARGET_ARCH_CYGWIN))
            {
                
                char fname[256];

                /* mod here (variable "fname") no longer in use. --milne */
                sprintf(fname, "/dev/console"); 
                fd = open (fname, O_RDWR);
                notice2(0, "open dev console fd = %d\n", fd);
                if (fd < 0)
                {
                    sprintf(fname, "/dev/tty");
                    fd = open (fname, O_RDWR);
                }
                notice2(0, "open dev tty fd = %d\n", fd);
                if (fd < 0)
                {
                    sprintf(fname, "/dev/null");
                    fd = open (fname, O_RDWR);
                }
                notice3(0, "open %s fd = %d\n", fname, fd);
                (void) dup2(2, 1); /* point out at stderr or log */

#               if HAVE_SETSID
                (void) setsid();
#               endif
                (void) setpgrp();
            }
#           else
            {
                (void) open("/dev/null", O_RDONLY);
                (void) dup2(2, 1); /* point stdout to stderr */
                fd = open("/dev/tty", O_RDWR);
                if (fd >= 0)
                {
                    ioctl(fd, TIOCNOTTY, 0);
                    (void) close(fd);
                }
            }
#           endif
        }

        /* stderr is either the logfile, the users stderr or the /dev/null */
        /* stdout is either the logfile, the users stdout or the /dev/null */

        if (pidpath != NULL)
        {
            FILE * pidfile;

            pidfile = fopen(pidpath, "w");
            if (pidfile)
            {
                fprintf(pidfile, "%d\n", (int) getpid());
                fclose(pidfile);
            }
        }

        while (1)
        {
            connection_fd = net_accept(listener_fd);
	    reqnr++;

            pid = fork();

            if (pid < 0)
            {
                failure2(FAILED_SERVER, "Fork failed: %s\n", strerror(errno));
            }

            if (pid == 0)
            {
                pidpath = NULL;
                (void) setsid();
#               if defined(__hpux) || defined(TARGET_ARCH_SOLARIS)
                {
                    (void) setpgrp();
                }
#               else
                {
                    ttyfd = open("/dev/tty",O_RDWR);
                    if (ttyfd >= 0)
                    {
#                       if !defined(CRAY) && !defined(TARGET_ARCH_CYGWIN)
                        {
                            ioctl(ttyfd, TIOCNOTTY, 0);
                        }
#                       endif
                        close(ttyfd);
                    }
                }
#               endif
                
                fclose(stdin); /* take care of stream buffers too */
                close(0);
                close(listener_fd);
                listener_fd = -1;

                dup2(connection_fd, 0);
                fcntl(connection_fd, F_SETFD, 1);
                /* this should work, but not sure !? */
                /* Reports say it is needed on some systems */
                *stdin = *fdopen(0,"r"); /* reopen stdin  we need this since */
                doit();
                exit(0);
            }
            close(connection_fd);
        }
        if (pidpath != NULL)
        {
            remove(pidpath);
        }
    }

    rc = globus_module_deactivate(GLOBUS_GSI_GSS_ASSIST_MODULE);
    if(rc != GLOBUS_SUCCESS)
    {
        exit(1);
    }

    return 0;
}


/******************************************************************************
Function:       doit()
Description:    Assume stdin is connected to the socket.
                usrlog_fp is set to the logfile.
                Authenticate the client, create the child process, and pass
                the data on to the child process via its stdin. 
Parameters:
Returns:
******************************************************************************/
static void doit()
{
    int                                 p1[2];
    int                                 pid = 0;
    int                                 n;
    int                                 i;
    int                                 service_uid;
    int                                 service_gid;
    int                                 close_on_exec_read_fd;
    int                                 close_on_exec_write_fd;
    char                                buf[1024];
    char *                              s;
    char **                             args;
    char *                              argnp;
    char *                              execp;
    int                                 num_service_args = SERVICE_ARGS_MAX;
    char *                              service_args[SERVICE_ARGS_MAX];
    int                                 num_service_options =
        SERVICE_OPTIONS_MAX;
    char *                              service_options[SERVICE_OPTIONS_MAX];
    int                                 service_option_local_cred = 0;
    int                                 service_option_stderr_log = 0;
    int                                 service_option_accept_limited = 0;
    unsigned char                       int_buf[4];
    struct stat                         statbuf;
    char *                              service_line = NULL;
    char *                              service_path;
    char *                              gram_k5_path; 
    globus_sockaddr_t                   peer;
    globus_socklen_t                    peerlen;
    char *                              peernum = "";
    char *                              x509_delegate;
    size_t                              length;
    FILE *                              http_body_file;
    /* GSSAPI assist variables */
    OM_uint32                           major_status = 0;
    OM_uint32                           minor_status = 0;
    int                                 token_status = 0;
    OM_uint32                           ret_flags = 0;
    gss_buffer_desc                     context_token = GSS_C_EMPTY_BUFFER;
    FILE *                              context_tmpfile = NULL;

    /* Authorization variables */
    int                                 rc;
    globus_result_t                     result;
    char *                              client_name;
    char                                identity_buffer[256];
    char *                              userid = NULL;
    struct passwd *                     pw;
    char *                              mapping = NULL;
    int                                 proxy_socket[2] = {-1, -1};

    /* HTTP messaging */
    char                               *header, *body;
    size_t                              body_length;

    {
        struct sigaction act;
        act.sa_handler = terminate;
        sigemptyset(&act.sa_mask);
        sigaddset(&act.sa_mask, SIGALRM);
        act.sa_flags = 0;
        sigaction(SIGALRM, &act, NULL);
    }

    /* TODO: Stop mucking with stdin and stdout file streams */
    fclose(stdout);
    close(1);
    dup2(0,1);
    *stdout = *fdopen(1,"w");
    (void) setbuf(stdout,NULL);

    peerlen = sizeof(peer);
    if (getpeername(0, (struct sockaddr *) &peer, &peerlen) == 0)
    {

        if (getnameinfo((struct sockaddr *) &peer, peerlen, &buf[0], (globus_socklen_t) sizeof(buf),
                NULL, 0, NI_NUMERICHOST) == 0)
        {
            peernum = strdup(buf);
        }
    }

    fdout = fdopen(1,"w"); /* establish an output stream */
    setbuf(fdout,NULL);

    notice3(LOG_INFO, "Got connection %s at %s", peernum, timestamp());

    /* 
     * if globus nologin is set, error message and exit
     */
        
    if (access("/etc/nologin", F_OK) == 0)
    {
        failure(FAILED_NOLOGIN, 
                "Not accepting connections at this time (/etc/nologin)");
    }

    if (getenv("GLOBUS_LOCATION") != NULL &&
        access(genfilename(gatekeeperhome,"etc",globusnologin), F_OK) == 0)
    {
        failure(FAILED_NOLOGIN, 
                "Not accepting connections at this time ($GLOBUS_LOCATION/etc/nologin)");
    }

    if (access("/etc/globus-nologin", F_OK) == 0)
    {
        failure(FAILED_NOLOGIN, 
                "Not accepting connections at this time (/etc/globus-nologin)");
    }

    /* 
     * We will not accept limited proxies for authentication, as
     * they may have been stolen. This enforces part of the 
     * Globus security policy.
     * This check will be made later, after the service is
     * is determined. This is done to allow limited proxies
     * to be used for some services, like GARA
     */
    
    alarm(600);
    major_status = globus_gss_assist_accept_sec_context(
        &minor_status,
        &context_handle,
        credential_handle,
        &client_name,
        &ret_flags,
        NULL,            /* don't need user_to_user */
        &token_status,
        &delegated_cred_handle,
        globus_gss_assist_token_get_fd,
        (void *)stdin,
        globus_gss_assist_token_send_fd,
        (void *)fdout);

    if (major_status != GSS_S_COMPLETE)
    {
        if (logging_usrlog) 
        {
            globus_gss_assist_display_status(usrlog_fp,
                                             "GSS authentication failure ",
                                             major_status,
                                             minor_status,
                                             token_status);
        }

        failure4(FAILED_AUTHENTICATION,
                 "GSS failed Major:%8.8x Minor:%8.8x Token:%8.8x\n",
                 major_status,minor_status,token_status);
    }

    if (!(ret_flags & GSS_C_TRANS_FLAG))
    {
        /* Can't export context, must proxy the connection */
        launch_method = FORK_AND_PROXY;
    }
    
    /* now OK to send wrapped error message */
    ok_to_send_errmsg = 1;

    /* client_name is the globusID of the client, we need to check 
     * the gridmap file to see which local ID this is and if the
     * the user is authorized.
     */

    notice2(LOG_NOTICE, "Authenticated globus user: %s", client_name);

    /* End of authentication */

    read_header_and_body(context_handle, &header, &body, &body_length);

    /* parse of HTTP post line */
    {
        char * parse_re = "^POST (ping)?/([^@ ]+)(@([^ \n]+))?";
        regex_t reg;
        regmatch_t matches[5];

        rc = regcomp(&reg, parse_re, REG_EXTENDED);
        if (rc < 0)
        {
            failure(FAILED_SERVICELOOKUP,
                    "Error compiling parser expression\n");
        }
        rc = regexec(&reg, header, 5, matches, 0);
        if (rc < 0)
        {
            failure(FAILED_SERVICELOOKUP,
                    "Error parsing service line\n");
        }

        if (matches[1].rm_so != -1)
        {
            got_ping_request = 1;
        }

        if (matches[2].rm_so != -1)
        {
            size_t matchlen = matches[2].rm_eo - matches[2].rm_so;
            service_name = malloc(matchlen + 1);
            memcpy(service_name, header + matches[2].rm_so, matchlen);
            service_name[matchlen] = 0;
        }
        else
        {
            failure(FAILED_SERVICELOOKUP, "Error parsing service_line\n");
        }

        if (matches[4].rm_so != -1)
        {
            size_t matchlen = matches[4].rm_eo - matches[4].rm_so;
            mapping = malloc(matchlen + 1);

            memcpy(mapping, header + matches[4].rm_so, matchlen);
            mapping[matchlen] = 0;
        }
    }

    /*
     * Cook up a unique ID such that we can link the GSI info logged by
     * the Gatekeeper to the batch system info logged by the Job Manager.
     */
    {
	time_t       clock;
	struct tm  * tmp;
	const char * gk_jm_id_var = "GATEKEEPER_JM_ID";
	char         gatekeeper_jm_id[64];

	time(&clock);
	tmp = localtime(&clock);

	sprintf(gatekeeper_jm_id, "%04d-%02d-%02d.%02d:%02d:%02d.%010u.%010u",
	    tmp->tm_year + 1900, tmp->tm_mon + 1, tmp->tm_mday,
	    tmp->tm_hour, tmp->tm_min, tmp->tm_sec,
	    gatekeeper_pid & 0xFFFFFFFF, reqnr & 0xFFFFFFFF);

	globus_libc_setenv(gk_jm_id_var, gatekeeper_jm_id, 1);
	globus_libc_setenv("GATEKEEPER_PEER", peernum, 1);

	notice5(0, "%s %s for %s on %s", gk_jm_id_var, gatekeeper_jm_id,
	    client_name, peernum);
    }

    /*
     * now that we know the desired service, do authorization
     * i.e. globus userid must be in the in the gridmap file. 
     */

    result = globus_gss_assist_map_and_authorize(context_handle,
                                                 service_name,
                                                 mapping,
                                                 identity_buffer, 256);

    if (result != GLOBUS_SUCCESS)
    {
        globus_object_t *               error;
        char *                          error_message = NULL;

        error = globus_error_get(result);
        error_message = globus_error_print_friendly(error);
        globus_object_free(error);
        failure2(FAILED_AUTHORIZATION,
                 "globus_gss_assist_gridmap() failed authorization."
                 " %s\n", error_message);
    }
    else if (mapping != NULL)
    {
        strncpy(&identity_buffer[0], mapping, sizeof(identity_buffer));
        identity_buffer[sizeof(identity_buffer) - 1] = '\0';
    }

    userid = identity_buffer;
    
    length = strlen(service_name);
    
    if (length > 256)
    {
        failure(FAILED_SERVICELOOKUP, "Service name malformed");
    }

    notice3(LOG_NOTICE,
            "Requested service: %s %s",
            service_name,
            (got_ping_request) ? "[PING ONLY]" : "");

    /* Don't allow the client to look for service files outside of the 
     * service directory
     */
    if (strchr(service_name, '/') != NULL)
    {
        failure2(FAILED_SERVICELOOKUP, "Invalid service name %s", service_name);
    }

    if ((rc = globus_gatekeeper_util_globusxmap(
		genfilename(getenv("GLOBUS_LOCATION") ? gatekeeperhome : "",grid_services,service_name), 
		NULL, 
		&service_line)) != 0)
      {
	    failure3(FAILED_SERVICELOOKUP,
		     "Failed to find requested service: %s: %d", 
		     service_name, rc);
      }
    
    /* 
     * Parse the command line.
     */ 
    if (globus_gatekeeper_util_tokenize(service_line,
                                        service_args, 
                                        &num_service_args,
                                        " \t\n"))
    {
        notice(LOG_ERR, "ERROR:Tokenize failed for services");
        failure(FAILED_SERVER, "ERROR: gatekeeper misconfigured");
    }
    
    if (num_service_args < SERVICE_ARG0_INDEX)
    {
        notice(LOG_ERR, "ERROR:To few service arguments");
        failure(FAILED_SERVER, "ERROR: gatekeeper misconfigured");
    }

    /* now check for options */
    if (globus_gatekeeper_util_tokenize(
            service_args[SERVICE_OPTIONS_INDEX],
            service_options,
            &num_service_options,
            ","))
    {
        notice(LOG_ERR, "ERROR:Tokenize failed for services options");
        failure(FAILED_SERVER, "ERROR: gatekeeper misconfigured");
    }
        
    for (i = 0; i < num_service_options; i++)
    {
        if (strcmp(service_options[i], "local_cred") == 0)
        {
            service_option_local_cred = 1;
        }
        else if (strcmp(service_options[i], "stderr_log") == 0)
        {
            service_option_stderr_log = 1;
        }
        else if (strcmp(service_options[i], "accept_limited") == 0)
        {
            service_option_accept_limited = 1;
        }
        else if (strcmp(service_options[i], "-") == 0)
        {
        }
        else
        {
            notice2(LOG_ERR, "ERROR:Invalid service option %s",
                    service_options[i]);
            failure(FAILED_SERVER, 
                    "ERROR: gatekeeper misconfigured");
        }
    }

    /*
     * most services will not acceot a limited proxy
     * for authentication. We will check now. This
     * only works with the Globus GSSAPI but is a noop
     * for the Kerberos GSSAPI
     */
#ifdef GSS_C_GLOBUS_LIMITED_PROXY_FLAG
    if ((service_option_accept_limited == 0)
        && (ret_flags & GSS_C_GLOBUS_LIMITED_PROXY_FLAG))
    {
        failure(FAILED_AUTHORIZATION, 
                "Attempt to use limited proxy for service");

    }
#endif


    /*
     * Either run as the userid from the globus map,
     *  or from grid_service as a selected user
     */
    
    if (strcmp(service_args[SERVICE_USER_INDEX],"-"))
    {
        userid = service_args[SERVICE_USER_INDEX];
    }
    
    notice2(LOG_NOTICE, "Authorized as local user: %s", userid);
    
    if ((pw = getpwnam(userid)) == NULL)
    {
        failure2(FAILED_SERVER, "getpwname() failed to find %s",userid);
    }

    service_uid = pw->pw_uid;
    {
        service_gid = pw->pw_gid;
    }

    notice2(LOG_NOTICE, "Authorized as local uid: %d", service_uid);
    notice2(LOG_NOTICE, "          and local gid: %d", service_gid);

    /*
     * service existed, we were authorized to use it. ping was successful.
     */
    if (got_ping_request)
    {
        failure(FAILED_PING, "ping successful");
    }

    if (delegated_cred_handle)
    {
        gss_buffer_desc                 deleg_proxy_filename;
        
        major_status = gss_export_cred(&minor_status,
                                       delegated_cred_handle,
                                       NULL,
                                       1,
                                       &deleg_proxy_filename);

        if (major_status == GSS_S_COMPLETE)
        {
            char *                      cp;

            cp = strchr((char *)deleg_proxy_filename.value, '=');
            *cp = '\0';
            cp++;
            globus_libc_setenv((char *)deleg_proxy_filename.value, cp, 1);
            free(deleg_proxy_filename.value);
        }
        else
        {
            char *                      error_str = NULL;
            globus_object_t *           error_obj;

            error_obj = globus_error_get((globus_result_t) minor_status);
            
            error_str = globus_error_print_friendly(error_obj);
            failure(FAILED_SERVER, error_str);
        }
    }

    /* for gssapi_ssleay if we received delegated proxy certificate
     * they will be in a file in tmp pointed at by the 
     * X509_USER_DELEG_PROXY env variable. 
     * This will be owned by the current uid, we need this owned by
     * the user.
     * for other gss, this is a noop, since X509_USER_DELEG_PROXY 
     * should not be defined.
     */

    {
        char *proxyfile;
        if ((proxyfile = getenv("X509_USER_PROXY")) != NULL)
        {
            chown(proxyfile,service_uid,service_gid);
        }
    }

    /* For the Kerberos GSSAPI  prior to 1.1 with the mode to 
     * src/lib/gssapi/krb5/accept_sec_context.c 
     * to setenv KRB5CCNAME, the KRB5CCANME will be 
     * owned by root, and we need to change the ownership. 
     */
    {
        char *ccname;
        if ((ccname = getenv("KRB5CCNAME")) != NULL
            && strlen(ccname) > 5 
            && !strncmp(ccname,"FILE:",5))
        {
            chown(ccname+5,service_uid,service_gid);
        }
    }

    service_path = genfilename(libexecdir,
                               service_args[SERVICE_PATH_INDEX],NULL);
        
    /*
     * Replace the unused arg0 much like wrapper does
     * generate a arg0 from the path later
     * arg0 is really only there since inetd.conf has it. 
     */

    service_args[SERVICE_ARG0_INDEX] = service_path; 

    if (stat(service_path, &statbuf) != 0)
    {
        notice2(LOG_ERR, "ERROR: Cannot stat globus service %s.",
                service_path);
        failure(FAILED_SERVER, "ERROR: gatekeeper misconfigured");
    }

    if (!(statbuf.st_mode & 0111))
    {
        notice2(LOG_ERR, "ERROR: Cannot execute globus service %s.",
                service_path);
        failure(FAILED_SERVER, "ERROR: gatekeeper misconfigured");
    }

    /*
     * Start building the arg list. If the -k flag is set, we want
     * to exec GRAM_K5_EXE first, passing it the path and
     * args for the service. 
     * If run from inetd, then the executables will be in libexecdir
     * otherwise they are in the current directory. 
     * we need absolute path, since we will do a chdir($HOME)
     */
        

    gram_k5_path = genfilename(libexecdir, GRAM_K5_EXE, NULL);

    /* need k5 plus the number of args in the service_args + NULL */
    /* we will overlay the previous service arg to do this. */
    /* this should be the SERVICE_PATH_INDEX */

    args = &service_args[SERVICE_ARG0_INDEX];

    if (krb5flag && service_option_local_cred)
    {
        args--;
        if (access(gram_k5_path, X_OK) != 0)
            failure2(FAILED_SERVER, "Cannot execute %s", gram_k5_path);
        *args = gram_k5_path;
    }

    notice2(0, "executing %s", args[0]);

    /*
     * Create two pipes to connect us to the servic:
     *   One is used with close-on-exec to sense child creation
     *          and pass back any error messages from the child
     *          to the parent.
     *DEE The above is obsolete, as gram_k5 gets execed next. 
     *   One is connected to the child's stdin, for passing a
     *          the message from the partent to the child.
     */

    if (pipe(p1) != 0)
    {
        failure2(FAILED_SERVER, "Cannot create pipe: %s", strerror(errno));
    }
    close_on_exec_read_fd = p1[0];
    close_on_exec_write_fd = p1[1];

    if (fcntl(close_on_exec_write_fd, F_SETFD, 1) != 0 ||
	fcntl(close_on_exec_read_fd, F_SETFD, 1) != 0)
    {
        failure2(FAILED_SERVER, "fcntl F_SETFD failed: %s", strerror(errno));
    }

    globus_libc_setenv("GLOBUS_ID",client_name,1);
    globus_libc_setenv("GRID_ID",client_name,1);
    globus_libc_setenv("GRID_AUTH_METHOD","TO_FILLED_IN_LATER",1);

    /*
     * Become the appropriate user
     */
    if (gatekeeper_uid == 0)
    {
        globus_libc_setenv("USER",userid,1);
        globus_libc_setenv("LOGNAME",userid,1);
        globus_libc_setenv("LOGIN",userid,1);
        globus_libc_setenv("HOME",pw->pw_dir,1);
        globus_libc_setenv("SHELL",pw->pw_shell,1);
    }
    /* 
     * Could set path, and other variables as well 
     * Unset many of the gssapi set env variables. 
     * If not present won't hurt to unset. 
     * Leave the X509_CERT_DIR of trusted certs
     * for the user to use. 
     */
    globus_libc_unsetenv("GRIDMAP"); /* unset it */
    globus_libc_unsetenv("GLOBUSCERTDIR"); /* unset it */
    globus_libc_unsetenv("GLOBUSKEYDIR"); /* unset it */
    globus_libc_unsetenv("X509_USER_KEY"); /* unset it */
    globus_libc_unsetenv("X509_USER_CERT"); /* unset it */

    /* 
     * If the gssapi_ssleay did delegation, promote the
     * delegated proxy file to the user proxy file
     */
    if ((x509_delegate = getenv("X509_USER_DELEG_PROXY")))
    {
        globus_libc_setenv("X509_USER_PROXY",strdup(x509_delegate),1);
        globus_libc_unsetenv("X509_USER_DELEG_PROXY");
    }

    if (gatekeeper_uid == 0)
    {

        /* do all the work to run as user, unless gram_k5 will do it for us */

        if (!(krb5flag && service_option_local_cred))
        {
            char * errmsg = NULL;
            int rc;
            if ((rc = globus_gatekeeper_util_trans_to_user(pw, userid, &errmsg)) != 0)
            {
                failure3(FAILED_SERVER, 
                         "trans_to_user: %d: %s", rc, errmsg);
            }
        }
    }

    if ((ret_flags & GSS_C_TRANS_FLAG) && (launch_method != FORK_AND_PROXY))
    {
        /* 
         * export the security context which will destroy it. 
         * This will also destroy the ability to wrap any error
         * messages, so we do this very late. 
         * First we get an temp file, open it, and delete it. 
         */

        context_tmpfile = tmpfile();
        if (context_tmpfile) 
        {
            setbuf(context_tmpfile,NULL);
            fcntl(fileno(context_tmpfile), F_SETFD, 0);
            sprintf(buf, "%d", fileno(context_tmpfile));
            globus_libc_setenv("GRID_SECURITY_CONTEXT_FD", buf, 1);
            notice2(0,"GRID_SECURITY_CONTEXT_FD=%s",buf);
        }
        else
        {
            failure(FAILED_SERVER, "Unable to create context tmpfile");
        }

        major_status = gss_export_sec_context(&minor_status,
                                              &context_handle, 
                                              &context_token);

        if (major_status != GSS_S_COMPLETE) 
        {
            globus_gss_assist_display_status(stderr,
                                             "GSS failed exporting context: ",
                                             major_status,
                                             minor_status,
                                             0);
            failure(FAILED_SERVER, "GSS Failed exporting context");
        }
            
        int_buf[0] = (unsigned char)(((context_token.length)>>24)&0xff);
        int_buf[1] = (unsigned char)(((context_token.length)>>16)&0xff);
        int_buf[2] = (unsigned char)(((context_token.length)>> 8)&0xff);
        int_buf[3] = (unsigned char)(((context_token.length)    )&0xff);
        
        if (fwrite(int_buf,4,1,context_tmpfile) != 1)
        {
            failure(FAILED_SERVER, "Failure writing context length");
        }
        if (fwrite(context_token.value,
                   context_token.length,
                   1,
                   context_tmpfile) != 1)
        {
            failure(FAILED_SERVER, "Failure writing context token");
        }

        gss_release_buffer(&minor_status,&context_token);

        /* reposition so service can read */

        lseek(fileno(context_tmpfile), 0, SEEK_SET);
    }
    if (launch_method == FORK_AND_PROXY)
    {
        rc = socketpair(AF_UNIX, SOCK_STREAM, 0, proxy_socket);
        if (rc != GLOBUS_SUCCESS ||
                proxy_socket[0] == -1 || proxy_socket[1] == -1)
        {
            failure(FAILED_SERVER, "Failed creating proxy socket");
        }
    }

    if (! got_ping_request)
    {
        http_body_file = tmpfile();
        if (http_body_file)
        {
            setbuf(http_body_file,NULL);
            fcntl(fileno(http_body_file), F_SETFD, 0);
            sprintf(buf, "%d", fileno(http_body_file));
            globus_libc_setenv("GRID_SECURITY_HTTP_BODY_FD", buf, 1);
            notice2(0,"GRID_SECURITY_HTTP_BODY_FD=%s",buf);

            fwrite(body,
                   1,
                   body_length,
                   http_body_file);

            lseek(fileno(http_body_file), 0, SEEK_SET);
        }    
        else
        {
            failure(FAILED_SERVER, "Unable to create http body tmpfile");
        }
    }

    chdir(pw->pw_dir);

    if ( launch_method != DONT_FORK )
    {
	pid = fork();
	if (pid < 0)
	{
	    failure2(FAILED_SERVER, "fork failed: %s", strerror(errno));
	}
    }

    if (launch_method == DONT_FORK)
    {
	notice2(0, "Starting child %d", pid);
    }

    if (pid == 0 || launch_method == DONT_FORK)
    {
        close(close_on_exec_read_fd);
        
        /* stderr is still set to logfile, user's stderr or /dev/null */

        /*
         * convert arg[0] to path and simple name for exec
         */

        execp = args[0];
        argnp = strrchr(args[0], '/'); 
        
        if (argnp)
            argnp++;
        else
            argnp = execp;
        args[0] = argnp;
        
        /*
         * If grid_service wanted stderr pointing at our log 
         * skip this part
         * otherwise point it at the socket
         */

        if (service_option_stderr_log == 0)
        {
            fclose(stderr);
            close(2);
            dup2(0,2);
            *stderr = *fdopen(2,"w");
            (void) setbuf(stderr,NULL);
        }

        if (launch_method == FORK_AND_PROXY)
        {
            char *tmp, *tmp2;
            const char * host = "\r\nHost:";
            gss_buffer_set_t buffer_set;

            globus_libc_setenv("REMOTE_ADDR", peernum, 1);
            globus_libc_setenv("REQUEST_METHOD", "POST", 1);
            globus_libc_setenv("SCRIPT_NAME", service_name, 1);

            tmp = globus_common_create_string("%zu", body_length);
            globus_libc_setenv("CONTENT_LENGTH", tmp, 1);
            notice2(0, "Set CONTENT_LENGTH=%s", tmp);

            globus_libc_setenv("GATEWAY_INTERFACE", "CGI/1.1", 1);
            notice(0, "Set GATEWAY_INTERFACE to CGI/1.1");

            /*
             * returns a sequence of DER-encoded certificates in the buffer_set
             */
            major_status = gss_inquire_sec_context_by_oid(
                &minor_status,
                context_handle,
                gss_ext_x509_cert_chain_oid,
                &buffer_set);

            if (major_status == GSS_S_COMPLETE)
            {
                const unsigned char * p;
                X509 *c;
                BIO *b;
                BUF_MEM *bptr;
                char * pemtext;
                b = BIO_new(BIO_s_mem());

                for (i = 0; i < buffer_set->count; i++)
                {
                    char * varname;
                    
                    if (i == 0)
                    {
                        varname = "SSL_CLIENT_CERT";
                    }
                    else
                    {
                        varname = globus_common_create_string(
                                "SSL_CLIENT_CERT_CHAIN%d",
                                i);
                    }
                    p = buffer_set->elements[i].value;
                    c = d2i_X509(NULL, GT_D2I_DATA_CAST &p, buffer_set->elements[i].length);

                    PEM_write_bio_X509(b, c);

                    BIO_get_mem_ptr(b, &bptr);
                    pemtext = globus_common_create_string(
                            "%.*s",
                            bptr->length,
                            bptr->data);
                    globus_libc_setenv(varname, pemtext, 1);
                    if (i == 0)
                    {
                        globus_libc_setenv("SSL_CLIENT_CERT_CHAIN0", pemtext, 1);
                    }
                    (void) BIO_reset(b);
                }
                BIO_free(b);
            }

            tmp = strstr(header, host);
            if (tmp != NULL)
            {
                tmp += strlen(host);
                while (isspace(*tmp))
                {
                    tmp++;
                }
                tmp2 = strstr(tmp, "\r");
                if (tmp && tmp2)
                {
                    tmp = globus_common_create_string("%.*s", (int)(tmp2-tmp), tmp);
                    globus_libc_setenv("SERVER_NAME", tmp, 1);
                    notice2(0, "Set SERVER_NAME to %s", tmp);
                }
            }
            tmp = globus_common_create_string("%d", daemon_port);
            globus_libc_setenv("SERVER_PORT", tmp, 1);
            notice2(0, "Set SERVER_PORT to %s", tmp);


            close(proxy_socket[0]);
            /* stdin and stdout point to proxy socket, stderr may be the log */
            dup2(proxy_socket[1], 0);
            dup2(proxy_socket[1], 1);

            if (proxy_socket[1] > 1)
            {
                close(proxy_socket[1]);
            }
        }

        alarm(0);
        if (execv(execp, args) != 0)
        {
            sprintf(tmpbuf, "Exec failed: %s\n", strerror(errno));
            write(close_on_exec_write_fd, tmpbuf, strlen(tmpbuf));
	    if (launch_method != DONT_FORK)
	    {
		exit(0);
	    }
        }
    }

    close(close_on_exec_write_fd);
    close(proxy_socket[1]);
    
    /*
     * If the read_fd is closed without any data, then the
     * job manager was executed ok by the child process, due
     * to the close-on-exec flag being set on write_fd.
     *
     * If there is data on read_fd, then it is an error message
     * from the child process.
     */
    if ((n = read(close_on_exec_read_fd, buf, sizeof(buf))) > 0)
    {
        buf[n] = '\0';
        s = index(buf, '\n');
        if (s)
            *s = 0;
        failure2(FAILED_SERVER, "child failed: %s", buf);
    }
    if (n < 0)
    {
        failure(FAILED_SERVER, "child failed: error reading child fd");
    }

    if (launch_method == FORK_AND_PROXY)
    {
        ssize_t written = 0;

        do
        {
            ssize_t s = 0;
            s = write(proxy_socket[0], body + written, body_length - written);

            if (s > 0)
            {
                written += s;
            }
            if (s < 0 && errno != EINTR)
            {
                break;
            }

        } while (written < body_length);

        written = 0;
        while ((n = read(proxy_socket[0], buf, sizeof(buf))) > 0)
        {
            char header[] = "HTTP/1.1 200 Ok\r\n";
            if (written == 0)
            {
                char * reply = malloc(sizeof(header) + n);

                strcpy(reply, header);
                memcpy(reply + sizeof(header) - 1, buf, n);
                globus_gss_assist_wrap_send(&minor_status,
                                            context_handle,
                                            reply,
                                            sizeof(header) + n - 1,
                                            &token_status,
                                            globus_gss_assist_token_send_fd,
                                            fdout,
                                            logging_usrlog?usrlog_fp:NULL);
                written = sizeof(header) + n - 1;
                free(reply);
                
            }
            else
            {
                globus_gss_assist_wrap_send(&minor_status,
                                            context_handle,
                                            buf,
                                            n,
                                            &token_status,
                                            globus_gss_assist_token_send_fd,
                                            fdout,
                                            logging_usrlog?usrlog_fp:NULL);
                written += n;
            }
        }
        notice2(0, "Read %d bytes from proxy pipe", (int) written);
        if (written == 0 && n == 0)
        {
            char reply[] = "HTTP/1.1 400 Bad Request\r\nConnection: close\r\n\r\n";
            notice(0, "Writing bad request reply\n");
            globus_gss_assist_wrap_send(&minor_status,
                                        context_handle,
                                        reply,
                                        sizeof(reply)-1,
                                        &token_status,
                                        globus_gss_assist_token_send_fd,
                                        fdout,
                                        logging_usrlog?usrlog_fp:NULL);
        }
        close(proxy_socket[0]);
    }
    close(close_on_exec_read_fd);

    if (launch_method != DONT_FORK)
    {
	notice2(0, "Child %d started", pid);
    }
    alarm(0);

    ok_to_send_errmsg = 0;
} /* doit() */  

/******************************************************************************
Function:    net_accept()
Description: Accept a connection on socket skt and return fd of new connection. 
Parameters:
Returns:
******************************************************************************/
static int 
net_accept(int skt)
{
    globus_socklen_t   fromlen;
    int                skt2;
    int                gotit;
    globus_sockaddr_t  from;

    fromlen = sizeof(from);
    gotit = 0;

    while (!gotit)
    {
	fd_set         fdset1;
	fd_set         fdset2;
	struct timeval timeout;
	int            n;

	FD_ZERO(&fdset1);
	FD_ZERO(&fdset2);
	FD_SET(skt, &fdset1);
	FD_SET(skt, &fdset2);
	timeout.tv_sec = 60;
	timeout.tv_usec = 0;

	n = select(skt + 1, &fdset1, (fd_set *) 0, &fdset2, &timeout);

	if (n < 0 && errno != EINTR)
	{
	    error_check(n, "net_accept select");
	}
	else if (n > 0)
	{
            long flags;

	    skt2 = accept(skt, (struct sockaddr *) &from, &fromlen);

	    if (skt2 == -1)
	    {
		if (errno != EINTR && errno != EAGAIN && errno != EWOULDBLOCK)
		{
		    error_check(skt2, "net_accept accept");
		}
	    }
	    else
		gotit = 1;
            flags = fcntl(skt2, F_GETFL, 0);
            flags &= ~O_NONBLOCK;
            fcntl(skt2, F_SETFL, flags);
	}

	if (logrotate)
	{
	    time_t clock = time((time_t *) 0);
	    struct tm *tmp = localtime(&clock);
	    char buf[128];

	    sprintf(buf, "logfile rotating at %04d-%02d-%02d %02d:%02d:%02d",
		tmp->tm_year + 1900, tmp->tm_mon + 1, tmp->tm_mday,
		tmp->tm_hour, tmp->tm_min, tmp->tm_sec);

	    notice2(LOG_INFO, "%s", buf);

	    if (logging_usrlog)
	    {
		static int seqnr;
		char *logpath = genfilename(logdir, logfile, NULL);
		char *oldpath = malloc(strlen(logpath) + 64);

		sprintf(oldpath, "%s.%04d%02d%02d%02d%02d%02d.%d", logpath,
		    tmp->tm_year + 1900, tmp->tm_mon + 1, tmp->tm_mday,
		    tmp->tm_hour, tmp->tm_min, tmp->tm_sec, seqnr++);

		if (rename(logpath, oldpath) != 0)
		{
		    notice4(LOG_ERR, "ERROR: cannot rename %s to %s: %s",
			logpath, oldpath, strerror(errno));
		}
		else if (logging_startup() != 0)
		{
		    failure(FAILED_SERVER, "Logging restart failure");
		}
		else
		{
		    logging_phase2();
		    fclose(stdout);
		    (void) dup2(2, 1); /* point stdout to stderr */
		    *stdout = *fdopen(1, "w");
		    notice2(LOG_INFO, "Continuing from %s", oldpath);
		}

		free(logpath);
		free(oldpath);
	    }

	    new_acct_file();

	    logrotate = 0;
	}
    }

    return(skt2);
}


/******************************************************************************
Function:       net_setup_listener()
Description:    
Parameters:
Returns:
******************************************************************************/
static void 
net_setup_listener(int backlog,
                   int * port,
                   int * skt)
{
    globus_socklen_t                    addrlen;
    int                                 family;
    struct sockaddr *                   addr;
    struct sockaddr_in6                 addr6;
    struct sockaddr_in                  addr4;
    long                                flags;
    int                                 one=1;

    family = AF_INET6;
    *skt = socket(family, SOCK_STREAM, 0);
    if (*skt < 0 && errno == EAFNOSUPPORT)
    {
        /* Fall back to ipv4 */
        family = AF_INET;
        *skt = socket(family, SOCK_STREAM, 0);
    }
    error_check(*skt,"net_setup_anon_listener socket");

    flags = fcntl(*skt, F_GETFL, 0);
    flags |= O_NONBLOCK;
    fcntl(*skt, F_SETFL, flags);

    error_check(setsockopt(*skt, SOL_SOCKET, SO_REUSEADDR, (char *)&one, sizeof(one)),
                "net_setup_anon_listener setsockopt");

    switch (family)
    {
        case AF_INET6:
            addr6.sin6_family = AF_INET6;
            addr6.sin6_addr = in6addr_any;
            addr6.sin6_port = htons(*port);

            addrlen = sizeof(addr6);
            addr = (struct sockaddr *) &addr6;
            break;
        case AF_INET:
            addr4.sin_family = AF_INET;
            addr4.sin_addr.s_addr = INADDR_ANY;
            addr4.sin_port = htons(*port);
            addrlen = sizeof(addr4);
            addr = (struct sockaddr *) &addr4;
            break;
    }
    error_check(bind(*skt, addr, addrlen), "net_setup_anon_listener bind");

    error_check(listen(*skt, backlog), "net_setup_anon_listener listen");

    getsockname(*skt, addr, &addrlen);

    switch (family)
    {
        case AF_INET6:
            *port = ntohs(addr6.sin6_port);
            break;
        case AF_INET:
            *port = ntohs(addr4.sin_port);
            break;
    }
}

/******************************************************************************
Function:       logging_startup()
Description:
Parameters:
Returns:
******************************************************************************/
static int
logging_startup(void)
{
    if ((strncmp(logfile, "logoff", 6) == 0) ||
        (strncmp(logfile, "LOGOFF", 6) == 0))
    {
        /* no logging will be done, even syslog */
        fprintf(stderr, "No logging will be done, even syslog.\n");
        logging_syslog = 0;
        logging_usrlog = 0;
    }
    else
    {
	if (!logging_syslog)
	{
	    /*
	     * By default open syslogfile if it is not open already.
	     * All messages will have GRAM gatekeeper and include the PID.
	     * The messages will be treated like any other system daemon.
	     */
	    logging_syslog = 1;
	    openlog("GRAM-gatekeeper", LOG_PID, log_facility);
	}

        if (strlen(logfile) > 0) 
        {
            char * logfilename;
            /*
             * Open the user specified logfile
             */
                        
	    if (logging_usrlog)
	    {
		/* close previous logfile, if any */

		if (usrlog_fp)
		{
		    fclose(usrlog_fp);
		}

		logging_usrlog = 0;
	    }

            logfilename = genfilename(logdir, logfile, NULL);

            if ((usrlog_fp = fopen(logfilename, "a")) == NULL)
            {
                fprintf(stderr, "Cannot open logfile %s: %s\n",
                        logfilename, strerror(errno));
                syslog(LOG_ERR, "Cannot open logfile %s\n", logfilename);

                return(1);
            }

            free(logfilename);
            logging_usrlog = 1;
        }
    }
    return(0);
} /* logging_startup() */

/******************************************************************************
Function:       logging_phase2()
Description:
Parameters:
Returns:
******************************************************************************/
static int
logging_phase2(void)
{

    if (logging_usrlog) 
    {
	/*
	 * set stderr to the log file, to catch all fprintf(stderr,...
	 * and catch some from gram_k5, and job_manager
	 * But if testing gatekeeper, write to stderr instead. 
	 */

	if (!gatekeeper_test && usrlog_fp) {
	    int tmpfd = dup(fileno(usrlog_fp));	/* save copy of logfile fd */

	    fflush(stderr);

	    if (usrlog_fp != stderr)
	    {
		fclose(usrlog_fp);	/* this may still close fd 2! */
	    }

	    fclose(stderr);

	    dup2(tmpfd, 2);		/* reconnect fd 2 to logfile */
	    close(tmpfd);

	    *stderr = *fdopen(2, "w");	/* reinitialize stderr */
	}
	usrlog_fp = stderr;

	/*
	 * Set output to non-buffered mode
	 */
	setbuf(stderr, NULL);
    }
    return(0);
} /* logging_phase2() */

/******************************************************************************
Function:       failure()
Description:    
Parameters:
Returns:
******************************************************************************/
static void 
failure(int failure_type, char * s)
{

    OM_uint32        minor_status = 0;
    int              token_status = 0;

    fprintf(stderr,"Failure: %s\n", s);
    if (logging_syslog)
    {
        syslog(LOG_ERR, "%s\n", s);
    }
    if (logging_usrlog)
    {
        fprintf(usrlog_fp, "TIME: %s PID: %d -- Failure: %s\n", timestamp(), getpid(), s);
    }
    /* 
     * attempt to send back to the gram_client one final 
     * error message before quiting. 
     */
    if (ok_to_send_errmsg)
    {
        char * response;
        
        switch (failure_type)
        {
        case FAILED_AUTHORIZATION: 
            response = ("HTTP/1.1 403 Forbidden\015\012"
                        "Connection: close\015\012"
                        "\015\012");
            break;

        case FAILED_SERVICELOOKUP:
            response = ("HTTP/1.1 404 Not Found\015\012"
                        "Connection: close\015\012"
                        "\015\012");
            break;

        case FAILED_PING:
            response = ("HTTP/1.1 200 OK\015\012"
                        "Content-Type: application/x-globus-gram\015\012"
                        "Content-Length: 0\015\012"
                        "\015\012");
            break;

        case FAILED_TOOLARGE:
            response = ("HTTP/1.1 200 OK\015\012"
                        "Content-Type: application/x-globus-gram\015\012"
                        "Content-Length: 33\015\012"
                        "\015\012"
                        "protocol-version: 2\015\012"
                        "status: 10\015\012");
            break;
        case FAILED_SERVER:
        case FAILED_NOLOGIN:
        case FAILED_AUTHENTICATION:
        default: 
            response = ("HTTP/1.1 500 Internal Server Error\015\012"
                        "Connection: close\015\012"
                        "\015\012");
            break;
        }
        
        /* don't care about errors here */
        globus_gss_assist_wrap_send(&minor_status,
                                    context_handle,
                                    response,
                                    strlen(response) + 1,
                                    &token_status,
                                    globus_gss_assist_token_send_fd,
                                    fdout,
                                    logging_usrlog?usrlog_fp:NULL);
    }
    if (gatekeeper_test)
    {
        fprintf(stderr,"Gatekeeper test complete : Failure!\n");
    }

    /* Cleanup any delegated credential */

    if (delegated_cred_handle)
    {
        OM_uint32 minor_status2;
        gss_release_cred(&minor_status2, &delegated_cred_handle);
    }

    exit(1);
} /* failure() */

/******************************************************************************
Function:       notice()
Description:    
Parameters: prty is the syslog priority, but if = 0, then dont syslog. 
Returns:
******************************************************************************/
static void 
notice(int prty, char * s)
{
    if (logging_syslog && prty)
    {
        syslog(prty, "%s", s);
    }
    if (logging_usrlog)
    {
        fprintf(usrlog_fp, "TIME: %s PID: %d -- Notice: %d: %s\n", timestamp(), getpid(), prty, s);
    }
} /* notice() */


/******************************************************************************
Function:       error_check()
Description:    
Parameters:
Returns:
******************************************************************************/
static
void 
error_check(int val,
            char * str)
{
    if (val < 0)
    {
        failure3(FAILED_SERVER, 
                 "error check %s: %s\n", str, strerror(errno));
/*
  fprintf(usrlog_fp, "%s: %s\n",
  str,
  strerror(errno));
  exit(1);
*/
    }
} /* error_check() */

/******************************************************************************
Function:       timestamp()
Description:    
Parameters:
Returns:
******************************************************************************/
static
char *
timestamp(void)
{
    time_t clock;
    struct tm *tmp;

    time(&clock);
    tmp = localtime(&clock);
    return asctime(tmp);
} /* timestamp() */

static
void
read_header_and_body(
    gss_ctx_id_t                        context,
    char **                             header_out,
    char **                             body_out,
    size_t *                            bodylen_out)
{
    OM_uint32                           major_status, minor_status;
    char *                              tmp;
    size_t                              tmplen;
    char *                              header = NULL;
    size_t                              header_len = 0;
    char *                              body = NULL;
    size_t                              body_len = 0;
    int                                 have_header = 0;
    char *                              content_header;
    size_t                              content_length;
    int                                 token_status;

    do
    {
        major_status = globus_gss_assist_get_unwrap(
            &minor_status,
            context,
            &tmp,
            &tmplen,
            &token_status,
            globus_gss_assist_token_get_fd,
            stdin,
            logging_usrlog?usrlog_fp:NULL);

        if (!have_header)
        {
            char * t;

            t = realloc(header, header_len + tmplen + 1);
            if (!t)
            {
                failure(FAILED_SERVER, "Out of memory\n");
            }

            header = t;
            memcpy(header + header_len, tmp, tmplen);
            header_len += tmplen;
            header[header_len] = 0;

            if ((t = strstr(header, "\r\n\r\n")) != NULL)
            {
                int rc;
                have_header = 1;

                content_header = strstr(header, "\r\nContent-Length:");
                rc = sscanf(content_header + 2, "Content-Length: %zd", &content_length);
                if (rc < 1)
                {
                    failure(FAILED_SERVER, "No content-length header\n");
                }

                *t = '\0';

                if ((t + 4) <= header + header_len)
                {
                    t += 4;

                    body_len = header_len - (t - header);

                    body = malloc(body_len + 1);
                    memcpy(body, t, body_len);
                    body[body_len] = 0;

                    if (body_len >= content_length)
                    {
                        body_len = content_length;
                        break;
                    }
                }
            }
        }
        else
        {
            char * t;

            t = realloc(body, body_len + tmplen + 1);
            if (!t)
            {
                failure(FAILED_SERVER, "Out of memory\n");
            }
            body = t;
            memcpy(body + body_len, tmp, tmplen);
            body_len += tmplen;

            if (body_len >= content_length)
            {
                body_len = content_length;
                break;
            }
        }
    }
    while ((!GSS_ERROR(major_status)) && (body_len < 64000));

    if (major_status != GSS_S_COMPLETE)
    {
        failure4(FAILED_SERVICELOOKUP,
                 "Reading incoming message GSS failed Major:%8.8x "
                 "Minor:%8.8x Token:%8.8x\n",
                 major_status,
                 minor_status,
                 token_status);
    }
    else if (body_len >= 64000)
    {
        failure(FAILED_TOOLARGE, "Incoming message too large\n");
    }
     
    *header_out = header;
    *body_out = body;
    *bodylen_out = body_len;
}
/* read_header_and_body() */
