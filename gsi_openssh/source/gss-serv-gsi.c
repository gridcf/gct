/*
 * Copyright (c) 2001-2003 Simon Wilkinson. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR `AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "includes.h"

#ifdef GSSAPI
#ifdef GSI

#include <sys/types.h>

#include <stdarg.h>
#include <string.h>

#include "xmalloc.h"
#include "hostfile.h"
#include "auth.h"
#include "log.h"
#include "misc.h"
#include "servconf.h"
#include "ssh-gss.h"

extern ServerOptions options;

#include <globus_gss_assist.h>

static int ssh_gssapi_gsi_userok(ssh_gssapi_client *client, char *name);
static int ssh_gssapi_gsi_localname(ssh_gssapi_client *client, char **user);
static int ssh_gssapi_gsi_storecreds(ssh_gssapi_client *client);
static int ssh_gssapi_gsi_storecreds_micv2(ssh_gssapi_client *client);
static int ssh_gssapi_gsi_updatecreds(ssh_gssapi_ccache *store,
    ssh_gssapi_client *client);
static int ssh_gssapi_gsi_updatecreds_micv2(ssh_gssapi_ccache *store,
    ssh_gssapi_client *client);

ssh_gssapi_mech gssapi_gsi_mech = {
	"dZuIebMjgUqaxvbF7hDbAw==",
	"GSI",
	{9, "\x2B\x06\x01\x04\x01\x9B\x50\x01\x01"},
	NULL,
	&ssh_gssapi_gsi_userok,
	&ssh_gssapi_gsi_localname,
	&ssh_gssapi_gsi_storecreds,
	&ssh_gssapi_gsi_updatecreds
};

ssh_gssapi_mech gssapi_gsi_mech_micv2 = {
	"vz8J1E9PzLr8b1K+0remTg==",
	"GSI",
	{10, "\x2b\x06\x01\x04\x01\x9b\x50\x01\x01\x01"},
	NULL,
	&ssh_gssapi_gsi_userok,
	&ssh_gssapi_gsi_localname,
	&ssh_gssapi_gsi_storecreds_micv2,
	&ssh_gssapi_gsi_updatecreds_micv2
};

/*
 * Check if this user is OK to login under GSI. User has been authenticated
 * as identity in global 'client_name.value' and is trying to log in as passed
 * username in 'name'.
 *
 * Returns non-zero if user is authorized, 0 otherwise.
 */
static int
ssh_gssapi_gsi_userok(ssh_gssapi_client *client, char *name)
{
    int authorized = 0;
    globus_result_t res;
#ifdef HAVE_GLOBUS_GSS_ASSIST_MAP_AND_AUTHORIZE
    char lname[256] = "";
#endif

#ifdef GLOBUS_GSI_GSS_ASSIST_MODULE
    if (globus_module_activate(GLOBUS_GSI_GSS_ASSIST_MODULE) != 0) {
        return 0;
    }
#endif

/* use new globus_gss_assist_map_and_authorize() interface if available */
#ifdef HAVE_GLOBUS_GSS_ASSIST_MAP_AND_AUTHORIZE
    debug("calling globus_gss_assist_map_and_authorize()");
    if (GLOBUS_SUCCESS !=
        (res = globus_gss_assist_map_and_authorize(client->context, "ssh",
                                                   name, lname, 256))) {
        debug("%s", globus_error_print_chain(globus_error_get(res)));
    } else if (lname[0] && strcmp(name, lname) != 0) {
        debug("GSI user maps to %s, not %s", lname, name);
    } else {
        authorized = 1;
    }
#else
    debug("calling globus_gss_assist_userok()");
    if (GLOBUS_SUCCESS !=
        (res = (globus_gss_assist_userok(client->displayname.value,
                                         name)))) {
        debug("%s", globus_error_print_chain(globus_error_get(res)));
    } else {
        authorized = 1;
    }
#endif

    logit("GSI user %s is%s authorized as target user %s",
        (char *) client->displayname.value, (authorized ? "" : " not"), name);

    return authorized;
}

/*
 * Return the local username associated with the GSI credentials.
 */
int
ssh_gssapi_gsi_localname(ssh_gssapi_client *client, char **user)
{
    globus_result_t res;
#ifdef HAVE_GLOBUS_GSS_ASSIST_MAP_AND_AUTHORIZE
    char lname[256] = "";
#endif

#ifdef GLOBUS_GSI_GSS_ASSIST_MODULE
    if (globus_module_activate(GLOBUS_GSI_GSS_ASSIST_MODULE) != 0) {
        return 0;
    }
#endif

/* use new globus_gss_assist_map_and_authorize() interface if available */
#ifdef HAVE_GLOBUS_GSS_ASSIST_MAP_AND_AUTHORIZE
    debug("calling globus_gss_assist_map_and_authorize()");
    if (GLOBUS_SUCCESS !=
        (res = globus_gss_assist_map_and_authorize(client->context, "ssh",
                                                   NULL, lname, 256))) {
        debug("%s", globus_error_print_chain(globus_error_get(res)));
        logit("failed to map GSI user %s", (char *)client->displayname.value);
        return 0;
    }
    *user = strdup(lname);
#else
    debug("calling globus_gss_assist_gridmap()");
    if (GLOBUS_SUCCESS !=
        (res = globus_gss_assist_gridmap(client->displayname.value, user))) {
        debug("%s", globus_error_print_chain(globus_error_get(res)));
        logit("failed to map GSI user %s", (char *)client->displayname.value);
        return 0;
    }
#endif

    logit("GSI user %s mapped to target user %s",
        (char *) client->displayname.value, *user);

    return 1;
}

/*
 * Export GSI credentials to disk.
 */
static int
ssh_gssapi_gsi_storecreds(ssh_gssapi_client *client)
{
	OM_uint32	major_status;
	OM_uint32	minor_status;
	gss_buffer_desc	export_cred = GSS_C_EMPTY_BUFFER;
	char *		p;

	if (!client || !client->creds) {
		return 0;
	}

	major_status = gss_export_cred(&minor_status,
				       client->creds,
				       GSS_C_NO_OID,
				       1,
				       &export_cred);
	if (GSS_ERROR(major_status) && major_status != GSS_S_UNAVAILABLE) {
		Gssctxt *ctx;
		ssh_gssapi_build_ctx(&ctx);
		ctx->major = major_status;
		ctx->minor = minor_status;
		ssh_gssapi_set_oid(ctx, &gssapi_gsi_mech.oid);
		ssh_gssapi_error(ctx);
		ssh_gssapi_delete_ctx(&ctx);
		return 0;
	}

	p = strchr((char *) export_cred.value, '=');
	if (p == NULL) {
		logit("Failed to parse exported credentials string '%.100s'",
		    (char *)export_cred.value);
		gss_release_buffer(&minor_status, &export_cred);
		return 0;
	}
	*p++ = '\0';
	if (strcmp((char *)export_cred.value,"X509_USER_DELEG_PROXY") == 0) {
		client->store.envvar = strdup("X509_USER_PROXY");
	} else {
		client->store.envvar = strdup((char *)export_cred.value);
	}
	if (access(p, R_OK) == 0) {
		if (client->store.filename) {
			if (rename(p, client->store.filename) < 0) {
				logit("Failed to rename %s to %s: %s", p,
				    client->store.filename, strerror(errno));
				free(client->store.filename);
				client->store.filename = strdup(p);
			} else {
				p = client->store.filename;
			}
		} else {
			client->store.filename = strdup(p);
		}
	}
	client->store.envval = strdup(p);
#ifdef USE_PAM
	if (options.use_pam)
		do_pam_putenv(client->store.envvar, client->store.envval);
#endif
	gss_release_buffer(&minor_status, &export_cred);

	return 1;
}

/*
 * Export GSI credentials to disk.
 */
static int
ssh_gssapi_gsi_storecreds_micv2(ssh_gssapi_client *client)
{
	OM_uint32	major_status;
	OM_uint32	minor_status;
	gss_buffer_desc	export_cred = GSS_C_EMPTY_BUFFER;
	char *		p;

	if (!client || !client->creds) {
		return 0;
	}

	major_status = gss_export_cred(&minor_status,
				       client->creds,
				       GSS_C_NO_OID,
				       1,
				       &export_cred);
	if (GSS_ERROR(major_status) && major_status != GSS_S_UNAVAILABLE) {
		Gssctxt *ctx;
		ssh_gssapi_build_ctx(&ctx);
		ctx->major = major_status;
		ctx->minor = minor_status;
		ssh_gssapi_set_oid(ctx, &gssapi_gsi_mech_micv2.oid);
		ssh_gssapi_error(ctx);
		ssh_gssapi_delete_ctx(&ctx);
		return 0;
	}

	p = strchr((char *) export_cred.value, '=');
	if (p == NULL) {
		logit("Failed to parse exported credentials string '%.100s'",
		    (char *)export_cred.value);
		gss_release_buffer(&minor_status, &export_cred);
		return 0;
	}
	*p++ = '\0';
	if (strcmp((char *)export_cred.value,"X509_USER_DELEG_PROXY") == 0) {
		client->store.envvar = strdup("X509_USER_PROXY");
	} else {
		client->store.envvar = strdup((char *)export_cred.value);
	}
	if (access(p, R_OK) == 0) {
		if (client->store.filename) {
			if (rename(p, client->store.filename) < 0) {
				logit("Failed to rename %s to %s: %s", p,
				    client->store.filename, strerror(errno));
				free(client->store.filename);
				client->store.filename = strdup(p);
			} else {
				p = client->store.filename;
			}
		} else {
			client->store.filename = strdup(p);
		}
	}
	client->store.envval = strdup(p);
#ifdef USE_PAM
	if (options.use_pam)
		do_pam_putenv(client->store.envvar, client->store.envval);
#endif
	gss_release_buffer(&minor_status, &export_cred);

	return 1;
}

/*
 * Export updated GSI credentials to disk.
 */
static int
ssh_gssapi_gsi_updatecreds(ssh_gssapi_ccache *store,ssh_gssapi_client *client)
{
	return ssh_gssapi_gsi_storecreds(client);
}

/*
 * Export updated GSI credentials to disk.
 */
static int
ssh_gssapi_gsi_updatecreds_micv2(ssh_gssapi_ccache *store,ssh_gssapi_client *client)
{
	return ssh_gssapi_gsi_storecreds_micv2(client);
}

#endif /* GSI */
#endif /* GSSAPI */
