/*
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, and the entire permission notice in its entirety,
 *    including the disclaimer of warranties.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. The name of the author may not be used to endorse or promote
 *    products derived from this software without specific prior
 *    written permission.
 *
 * ALTERNATIVELY, this product may be distributed under the terms of
 * the GNU Public License, in which case the provisions of the GPL are
 * required INSTEAD OF the above restrictions.  (This clause is
 * necessary due to a potential bad interaction between the GPL and
 * the restrictions contained in a BSD-style copyright.)
 *
 * THIS SOFTWARE IS PROVIDED ``AS IS'' AND ANY EXPRESS OR IMPLIED
 * WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED
 * OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/*
 * Copyright (c) 2011 Red Hat, Inc.
 * Written by Tomas Mraz <tmraz@redhat.com>
*/

#define _GNU_SOURCE

#include "config.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <pwd.h>
#include <fcntl.h>
#include <unistd.h>
#ifdef HAVE_STDINT_H
#include <stdint.h>
#endif

#include <security/pam_appl.h>

#include "uidswap.h"
#include "misc.h"

#define ERR_USAGE 1
#define ERR_PAM_START 2
#define ERR_OPEN_SESSION 3
#define ERR_CLOSE_SESSION 4
#define ERR_PAM_END 5
#define ERR_GETPWNAM 6
#define ERR_MEMORY 7
#define ERR_OPEN 8
#define ERR_FILE_MODE 9
#define ERR_FDOPEN 10
#define ERR_STAT 11
#define ERR_WRITE 12
#define ERR_PAM_PUTENV 13
#define BUFLEN 4096

/* Just ignore the messages in the conversation function */
static int
dummy_conv(int num_msg, const struct pam_message **msgm,
	   struct pam_response **response, void *appdata_ptr)
{
	struct pam_response *rsp;

	(void)msgm;
	(void)appdata_ptr;

	if (num_msg <= 0)
		return PAM_CONV_ERR;

	/* Just allocate the array as empty responses */
	rsp = calloc (num_msg, sizeof (struct pam_response));
	if (rsp == NULL)
		return PAM_CONV_ERR;

	*response = rsp;
	return PAM_SUCCESS;
}

static struct pam_conv conv = {
	dummy_conv,
	NULL
};

char *
make_auth_keys_name(const struct passwd *pwd)
{
	char *fname;

	if (asprintf(&fname, "%s/.ssh/authorized_keys", pwd->pw_dir) < 0)
		return NULL;

	return fname;
}

int
dump_keys(const char *user)
{
	struct passwd *pwd;
	int fd = -1;
	FILE *f = NULL;
	char *fname = NULL;
	int rv = 0;
	char buf[BUFLEN];
	size_t len;
	struct stat st;

	if ((pwd = getpwnam(user)) == NULL) {
		return ERR_GETPWNAM;
	}

	if ((fname = make_auth_keys_name(pwd)) == NULL) {
		return ERR_MEMORY;
	}

	temporarily_use_uid(pwd);

	if ((fd = open(fname, O_RDONLY|O_NONBLOCK|O_NOFOLLOW, 0)) < 0) {
		rv = ERR_OPEN;
		goto fail;
	}

	if (fstat(fd, &st) < 0) {
		rv = ERR_STAT;
		goto fail;
	}

	if (!S_ISREG(st.st_mode) || 
		(st.st_uid != pwd->pw_uid && st.st_uid != 0)) {
		rv = ERR_FILE_MODE;
		goto fail;
	}

	unset_nonblock(fd);

	if ((f = fdopen(fd, "r")) == NULL) {
		rv = ERR_FDOPEN;
		goto fail;
	}

	fd = -1;

	while ((len = fread(buf, 1, sizeof(buf), f)) > 0) {
		rv = fwrite(buf, 1, len, stdout) != len ? ERR_WRITE : 0;
	}

fail:
	if (fd != -1)
		close(fd);
	if (f != NULL)
		fclose(f);
	free(fname);
	restore_uid();
	return rv;
}

static const char *env_names[] = { "SELINUX_ROLE_REQUESTED",
	"SELINUX_LEVEL_REQUESTED",
	"SELINUX_USE_CURRENT_RANGE"
};

extern char **environ;

int
set_pam_environment(pam_handle_t *pamh)
{
	int i;
	size_t j;

	for (j = 0; j < sizeof(env_names)/sizeof(env_names[0]); ++j) {
		int len = strlen(env_names[j]);

		for (i = 0; environ[i] != NULL; ++i) {
			if (strncmp(env_names[j], environ[i], len) == 0 &&
			    environ[i][len] == '=') {
				if (pam_putenv(pamh, environ[i]) != PAM_SUCCESS)
					return ERR_PAM_PUTENV;
			}
		}
	}

	return 0;
}

int
main(int argc, char *argv[])
{
	pam_handle_t *pamh = NULL;
	int retval;
	int ev = 0;

	if (argc != 2) {
		fprintf(stderr, "Usage: %s <user-name>\n", argv[0]);
		return ERR_USAGE;
	}

	retval = pam_start("ssh-keycat", argv[1], &conv, &pamh);
	if (retval != PAM_SUCCESS) {
		return ERR_PAM_START;
	}

	ev = set_pam_environment(pamh);
	if (ev != 0)
		goto finish;

	retval = pam_open_session(pamh, PAM_SILENT);
	if (retval != PAM_SUCCESS) {
		ev = ERR_OPEN_SESSION;
		goto finish;
	}

	ev = dump_keys(argv[1]);

	retval = pam_close_session(pamh, PAM_SILENT);
	if (retval != PAM_SUCCESS) {
		ev = ERR_CLOSE_SESSION;
	}

finish:
	retval = pam_end (pamh,retval);
	if (retval != PAM_SUCCESS) {
		ev = ERR_PAM_END;
	}
	return ev;
}
