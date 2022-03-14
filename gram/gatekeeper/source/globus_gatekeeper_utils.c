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

globus_gatekeeper_utils.c

Description:
	Some common routines used by globus_gatekeeper 
	and globus_gram_k5.c


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
#include <stdio.h>
#include <stdlib.h>
#include <locale.h>
#include <grp.h>
#include <pwd.h>
#include <string.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <errno.h>
#include <unistd.h>

#ifdef HAVE_MALLOC_H
#   include <malloc.h>
#endif

#include "globus_gatekeeper_utils.h"

/******************************************************************************
                               Type definitions
******************************************************************************/

/******************************************************************************
                          Module specific prototypes
******************************************************************************/

static
char * fgetscont(char *line, int size, FILE* fd);

/******************************************************************************
                       Define module specific variables
******************************************************************************/

static
char * fgetscont(char * line, int size, FILE* fd)
{
	int i;
	int len;
	char * cp;

	i = 2;
	len = size;
    cp = line;
	*cp = '\0';

	while(fgets(cp, len, fd) &&  
		(i = strlen(line)) > 2 && 
		line[i-1] == '\n' && line[i-2] == '\\') {
      len = size - i - 2;
	  cp = line + i - 2;
	}
	if (*cp == '\0') {
		return NULL;
	}
	return line;
}
/******************************************************************************
Function:   globus_gatekeeper_util_globusxmap()
Description:
	Given a index, find the command to be issued. For example
	this could be a service name, or a globusID, or K5 principal

Parameters:
	index
	A pointer to a char *. will strdup with command
Returns:
******************************************************************************/
int
globus_gatekeeper_util_globusxmap( char * filename, char * index, char ** command)
{

	FILE * fd;
	int    rc;
	int	   i;
    int    offset;
	char   f_index[256]; 
	char   line[4096];

	*command = NULL;

  if ((fd = fopen(filename, "r")) != NULL) {

    while(fgetscont(line, sizeof(line), fd)) {
      i = strlen(line);
	  if (line[0] != '#') {   /* comment line */
	    if (line[i - 1] == '\n') {
			line[i - 1] = '\0';
		}
                if (!index)
                {
                        *command = strdup(line);
                        fclose(fd);
                        return(0);
                }
 
		rc = sscanf(line, " \"%255[^\"]%*c%n %n", 
						f_index, &offset, &offset);
		if (rc != 1) {
        	rc = sscanf(line, "%255s%n %n",
					 f_index, &offset, &offset);
		}
        if (rc == 1) {
	      if (!strcmp(index, f_index)) {
		    *command = strdup(&line[offset]);
            fclose(fd);
		    return(0);
 
	      }
	    }
	  }
	}
	fclose(fd);
	return(-1); /* not found */	
  }
  return(-2);   /* open failed */
}
/******************************************************************************
Function:   globus_gatekeeper_util_tokenize()

Description:
	Breakup the command in to args, pointing the args array
	at the tokens. Replace white space at the end of each
	token with a null. A token maybe in quotes. 

Parameters:
	The command line to be parsed.
	A pointer to an array of pointers to be filled it
	Size of the array, on input, and set to size used on output. 

Returns:
	0 on success. 
	-1 on to malloc
	-2 on to many args
	-3 on quote not matched
******************************************************************************/

int
globus_gatekeeper_util_tokenize(char * command, 
								char ** args,
								int * n,
								char * sep)
{
  int i;
  char * cp;
  char * pp;
  char * qp;
  char ** arg;

  arg = args;
  i = *n - 1;

  cp = command;
  while (*cp)
  {
  	/* skip leading sep characters */
	while (*cp && strchr(sep, *cp))
	{
		cp++;
	}
	pp = NULL;
	if (*cp == '\"')
	{
		cp++;
		pp = cp;
		if ((qp = strchr(cp,'\"')) == NULL)
		{
			return -3;
		}
		cp = qp + 1;
		
	}
	else if (*cp)
	{
		pp = cp;
		if ((qp = strpbrk(cp,sep)) == NULL)
		{
			qp = strchr(cp,'\0');
		}
		cp = qp;
	}
	if (pp)
	{
		*arg = (char*)malloc((qp - pp) + 1);
		if (*arg == NULL)
		{
			return -1;
		}
		memcpy(*arg,pp,qp - pp);
		*(*arg + (qp - pp)) = '\0';
		i--;
    	if (i == 0)
	  		return(-2); /* to many args */
    	arg++;
  	}
  }
  *arg = (char *) 0;
  *n = *n - i - 1;
  return(0);
}

/******************************************************************************
Function:   globus_gatekeeper_util_envsub()

Description:
	Substitute from environment string like ${text} 
	into arg.  arg will be freed and copied.
	Recursion is allowed. 

Parameters:
	arg a pointer to the string pointer. 

Returns:
	0 on success. 
	-1 on malloc
	-3 on env string not found
******************************************************************************/

int
globus_gatekeeper_util_envsub(char ** arg)
{
	char * cp;
	char * pp;
	char * qp;
	char * rp;
	char * narg;
	
	cp = *arg;
	while ((pp = strstr(cp,"${")))  /* for editor matching } */
	{
		pp+=2;
									/* for editor matching { */
		if(!(qp = strstr(pp,"}")))
		{
			return -2;  /* not terminated */
		}
		*(pp-2) = '\0'; /* term the prefix */
		*qp = '\0';     /* term the env name */
		qp++;
		if (!(rp = getenv(pp)))
		{
			return -3;
		}
		if (!(narg = (char *)malloc(strlen(cp) + 
							   strlen(rp) + 
							   strlen(qp) + 1)))
		{
			return -1;
		}
		strcpy(narg,cp);
		strcat(narg,rp);
		strcat(narg,qp);
		free(cp);
		cp = narg;
		*arg = cp;
	}
	return 0;
}

/******************************************************************************
Function:   globus_gatekeeper_util_exec()
Description:
Parameters:
Returns:
******************************************************************************/
int
globus_gatekeeper_util_exec(char *args[], 
					struct passwd *pw, 
					char * user, 
					char **errmsgp)
{

  int pid;
  int err; 
  int rc;
  char *path;
  char * cp;

#define WAIT_USES_INT
#ifdef  WAIT_USES_INT
  int wait_status;
#else   /* WAIT_USES_INT */
  union wait wait_status;
#endif  /* WAIT_USES_INT */

  pid = fork();
  if (pid <0) 
   return(-1);

  if (pid == 0) {  /* child process */

	/* If need to run child as user */
	if (pw != NULL) {
		if ((rc = globus_gatekeeper_util_trans_to_user(pw, user, errmsgp)) != 0) { 
			/* child failed to transfer to user context */
			fprintf(stderr,"Failed trying to run as user %s: %s\n",
				user, *errmsgp);
			exit(126);
		}
	}
		
    path = strdup(args[0]);
    cp = strrchr(path, '/');
    if (cp)
      cp++;
    else
      cp = path;
 
    args[0] = cp;

#ifdef DEBUG
	fprintf(stderr,"EXECING %s args=",path);
	{
		int n = 0;
		while (args[n]) {
			fprintf (stderr," %s \n",args[n]);
			n++;
		}
	fprintf(stderr,"\n");
	}
#endif
    execv(path, args);
	fprintf(stderr,"Failed to exec the child\n");
    exit(127);      /* in case execl fails */
  } 

  /* parent, wait for child to finish */

  wait_status = 0;
#ifdef  HAVE_WAITPID
  err = waitpid((pid_t) pid, &wait_status, 0);
#else   /* HAVE_WAITPID */
  err = wait4(pid, &wait_status, 0, (struct rusage *) NULL);
#endif  /* HAVE_WAITPID */

  /* if it worked or failed, continue on. */
  return(wait_status);
}

/******************************************************************************
Function:   globus_gatekeeper_util_trans_to_user()
Description:
	Transition the process from root to the user, 
	doing all the operating system specific stuff. 

Parameters:
Returns:
	 0 if OK
	<0 if errno is set;
	>0 if some other error.
	 1 if not root
	DEE needs work
******************************************************************************/

int
globus_gatekeeper_util_trans_to_user(struct passwd * pw,
			 char * userid,
			 char ** errmsg)
{

	uid_t myuid;

	/* must be root to use this */

	if ((myuid = getuid()) != 0)
	{
		if (myuid == pw->pw_uid)
			return 0;   /* already running as the user */
		else
		{
			*errmsg = strdup("Can not run as another user");
			return 1;   /* can't run as another user */
		}
	}

	/*
	 *DEE If we are root and want to run as root, should we continue
	 * here or just exit? Some logging might be usefull. 
	 */
	setgid(pw->pw_gid);
	initgroups(pw->pw_name, pw->pw_gid);

#	if defined(__hpux)
	{
		if (setresuid(pw->pw_uid, pw->pw_uid, -1) != 0)
		{
			 *errmsg = strdup("cannot setresuid");
			 return -2;
		}
	}
#	elif defined(TARGET_ARCH_SOLARIS) || \
		 defined(TARGET_ARCH_BSD) || \
		 defined(TARGET_ARCH_CYGWIN)
	{
		if (setuid(pw->pw_uid) != 0)
		{
			*errmsg = strdup("cannot setuid");
			return -3;
		}
	}
#	else
	{
		if (seteuid(0) != 0)
		{
			*errmsg = strdup("cannot seteuid");
			return -4;
		}
	
		if (setreuid(pw->pw_uid, pw->pw_uid) != 0)
		{
			*errmsg = strdup("cannot setreuid");
			return -5;
		}
	}
#	endif

	return 0;
}
