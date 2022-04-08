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

/******************************************************************************

globus_gram_k5.c

Description:
	globus to Kerberos simple authentication module.

	When exec-ed by the gram_gatekeeper after
	authentiicating the globus user, this routine
	will attempt to issue a command for the user.
	This may be as simple as a kinit with a password,
	or can use the NCSA krb525 command, or the
	sslk5 command to use the X509 user proxy.

	The args passed to this routine will not be used,
	but will be passed onto the job manager. The first parameter
	must be the path to the job manager. (Much like what wrapper
	or inetd does.)

	It is expected that the environment will contain the
	GLOBUSID=globusid of the user and USER=userid for the local
	unix system.  This program is normaly run as root,
	and will seteuid before execing the other modules.

	If not run as root, the user should have started the gatekeeper,
	and should already have gotten a K5 credential.

	The parameters to use and the mapping for the
	globus to K5 user are located in the
	.globuskmap file.

	Format of the .globuskmap file:
		 "globus_user" <kinit command line >... including k5 principal
	The globus_user may be in "" if it has blanks, such as
	a X509 name.
	This is designed to be a simple interface, and no attempt
	to parse or use the command info is made.
	This allows for other commands to be used instead
	of kinit. Such as krb525 or sslk5

	This will only be attempted if the gatekeeper
	is run as root, as if the user has started
	the gatekeeper, then he should have a K5
	credentials already.

CVS Information:
	$Source$
	$Date$
	$Revision$
	$Author$

******************************************************************************/
/*****************************************************************************
Include header files
******************************************************************************/
#include "globus_config.h"
#include "globus_gatekeeper_config.h"

#include "globus_common.h"

#include <stdio.h>
#include <stdlib.h>
#include <locale.h>
#include <pwd.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/stat.h>

#ifdef HAVE_MALLOC_H
#include <malloc.h>
#endif

#include "globus_gatekeeper_utils.h"

/******************************************************************************
                               Type definitions
******************************************************************************/
#ifndef K5AFSLOGIN
#define K5AFSLOGIN "/krb5/sbin/k5afslogin"
#endif

#ifndef K5DCELOGIN
#define K5DCELOGIN "/krb5/sbin/k5dcelogin"
#if defined(sun)
#define K5DCELIB "/usr/lib/libdce.so"
#else
#define K5DCELIB "/usr/lib/libdce.a"
#endif
#endif

#ifdef DEBUG
#define DEEDEBUG(A) fprintf(stderrX,A)
#define DEEDEBUG2(A,B) fprintf(stderrX,A,B)
FILE *stderrX;
#else
#define DEEDEBUG(A)
#define DEEDEBUG2(A,B)
#endif


/******************************************************************************
                          Module specific prototypes
******************************************************************************/

/******************************************************************************
                       Define module specific variables
******************************************************************************/

/******************************************************************************
Function:   globus_gram_k5_kinit()
Description:
Parameters:
Returns:
******************************************************************************/
int
globus_gram_k5_kinit(char * globus_client,
                     struct passwd *pw,
                     char * user,
                     char ** errmsgp)
{

  int rc;
  int i;
  char ccname[100];
  char * command;
  char * args[100];
  struct stat stx;

  if ((rc = globus_gatekeeper_util_globusxmap(getenv("GLOBUSKMAP"),
                                              globus_client, &command)))
    return(rc); /* not found, or nothing to do */

  if (!command)
    return(0); /* no command */

  i = 100;
  if ((rc = globus_gatekeeper_util_tokenize(command, args, &i, " \t\n")))
    return(rc);

  if (args[0] == NULL)
    return(0); /* no command */

  i = 0;
  do {
    sprintf(ccname, "FILE:/tmp/krb5cc_p%d.%d", getpid(), i++);
  }
  while(stat(ccname + 5, &stx) == 0);

  globus_libc_setenv("KRB5CCNAME", ccname, 1);

  DEEDEBUG2("calling UTIL_exec: user: %s ", user);
  DEEDEBUG2("and uid %d\n", pw ? pw->pw_uid : -111111);

  rc = globus_gatekeeper_util_exec(args, pw, user, errmsgp);

  /*
   * Make sure the creds cache is owned by the user.
   */

  if (rc == 0 && getuid() == 0 && pw) {
    (void) chown(ccname + 5, pw->pw_uid, pw->pw_gid);
  }

  DEEDEBUG2("globus_gram_k5_exec rc = %d\n", rc);
  return(rc);
}

/******************************************************************************
Function:   main()
Description:
Parameters:
Returns:
******************************************************************************/
int
main(int argc, char *argv[])
{
    int i;
    int rc;
    char *ccname;
    char *globusid;
    char *user;
    char *newpath;
    char *cp;
    char **ap;
    char **newargv;
    struct stat stx;
    extern int optind;
    extern char *optarg;
    uid_t myuid;
    struct passwd *pw;
    char *errmsg = NULL;

#ifdef DEBUG
    stderrX = stderr;
    /* stderrX = fopen("/tmp/k5gram.debug","w"); */
#endif

    myuid = getuid();  /* get our uid, to see if we are root. */
    DEEDEBUG2("k5gram uid = %lu\n", myuid);

    user = getenv("USER");
    if (user == NULL)
        exit(6);
    DEEDEBUG2("USER = %s\n", user);

    pw = getpwnam(user);
    if (pw == NULL)
        exit(7);
    DEEDEBUG2("USERID = %lu\n",pw->pw_uid);

    /* if not root, must run as your self */
    if (myuid && (myuid != pw->pw_uid))
        exit(8);

    /* we will need to copy the args, and may add the k5declogin
     * and k5afslogin before. So get three extra.
     */

    if ((newargv = calloc(argc + 3, sizeof(argv[0]))) == NULL) {
        fprintf(stderr, "Unable to allocate new argv\n");
        exit(1);
    }
    ap = newargv;


#ifdef DEBUG
    {
      int i;
      fprintf(stderrX, "k5gram args: ");
      i = 0;
      while (argv[i]) {
        fprintf(stderrX, "%s ", argv[i]);
        i++;
      }
      fprintf(stderrX, "\n");
    }
#endif

    ccname = getenv("KRB5CCNAME");

    /* If there is a cache, then the user must have
     * started the gatekeeper on thier own.
     * Or they were running the K5 GSSAPI. So
     * don't try and get a K5 cache for them.
     */

    if (ccname == NULL) {

        globusid = getenv("GLOBUS_ID");
        if (globusid == NULL)
            goto done;  /* Can't do globus-to-k5 without the globusid */
        DEEDEBUG2("GLOBUSID = %s\n", globusid);

        if (globus_gram_k5_kinit(globusid, pw, user, &errmsg) == 0) {
            ccname = getenv("KRB5CCNAME");
        }

        /* even if the above failed, we want to continue */
    }

    if (ccname) {
        DEEDEBUG2("KRB5CCNAME = %s\n", ccname);

        /* test if this machine has DCE and k5dcelogin is available.
         * if so put the k5dcelogin program on the list to call
         */

        if ((stat(K5DCELIB, &stx) == 0) &&
            (stat(K5DCELOGIN, &stx) == 0)) {
            *ap++ = K5DCELOGIN;
            DEEDEBUG2("Will try %s\n", K5DCELOGIN);
        }

        /* if this system has AFS and not a NFS/AFS translator
         * put it on the list too
         */


        if ((stat("/afs", &stx) == 0) &&
            (stat(K5AFSLOGIN, &stx) == 0) &&
            (stat("/usr/vice/etc/ThisCell", &stx) == 0)) {
            *ap++ = K5AFSLOGIN;

            DEEDEBUG2("Will try %s\n", K5AFSLOGIN);
        }
    }


 done:
    globus_libc_unsetenv("GLOBUSKMAP"); /* dont pass on */

    /* before continuing on, if we were run as root,
     * we will get to user state.
     */

    if((rc = globus_gatekeeper_util_trans_to_user(pw, user, &errmsg))) {

        fprintf(stderr, "Failed to run %d as the user %s %s\n",
                rc, user, errmsg);
        exit(3); /* have to fail, since cant run as root */
    }

    /* copy over the rest of the argument list.
     * gram_gatekeeper will have placed the path to the job_manager
     * as arg[1].
     */

    for (i = 1; i<argc; i++) {
        *ap++ = argv[i];
    }
    *ap = 0;   /* null as last */

    /* newargv[0] has the fully qualified name of the program to exec.
     * either it is K5DCELOGIN, K5AFSLOGIN, or the job_manager program
     * this was specified by the gatekeeper
     * We will parse to get the new argv[0] and the path.
     * we then exec the program with the rest of the parameters.
     */

    if (newargv[0] == NULL)
    {
        fprintf(stderr, "Missing command to execute\n");
        exit(EXIT_FAILURE);
    }
    newpath = strdup(newargv[0]);

    cp = strrchr(newpath, '/');
    if (cp)
        cp++;
    else
        cp = newpath;

    newargv[0] = cp;

    DEEDEBUG2("calling the program %s \n", newpath);

#ifdef DEBUG
    /* fclose(stderrX); */
#endif

    execv(newpath, newargv);

    /* only reachable if execl fails */

    fprintf(stderr, "Exec of %s failed: \n", newpath);
    exit(1);
}
