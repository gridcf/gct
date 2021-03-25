/*
 * Copyright 1999-2008 University of Chicago
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

#include "gssapi.h"
#include "globus_gss_assist.h"

#define SIZEOF_ARRAY(a) (sizeof(a) / sizeof(a[0]))

typedef int (*test_case)(void);
typedef struct
{
    char * name;
    test_case func;
}
test_case_t;

#define TEST_CASE(x) {#x, x}

static gss_ctx_id_t                     init_ctx;
static gss_ctx_id_t                     accept_ctx;
static char *                           test_dn = "/DC=org/DC=doegrids/OU=People/UID=328453245/Email=john@doe.com/E=john@doe.com";
static char *                           test_dn2 = "/DC=org/DC=doegrids/OU=People/UID=328453245/Email=jdoe@doe.com/E=jdoe@doe.com";
static char *                           wrong_test_dn = "/DC=org/DC=doegrids/OU=People/UID=328453245";
static char *                           primary_username = "jdoe";
static char *                           secondary_username[] =
{
    "john_doe",
    "doeJohn"
};
static char *                           wrong_username = "notJohn";
static const char *                     gridmap_dir;

struct gridmap_lookup_result
{
    char *                          gridmap;
    char *                          dn;
    char *                          username;
    globus_bool_t                   success;
};

struct gridmap_all_globusid_result
{
    char *                          gridmap;
    char *                          username;
    char *                          test_dn1;
    char *                          test_dn2;
    globus_bool_t                   success;
};

char *create_file_path(const char * dir, const char * filename)
{
#if defined(_WIN32)
#define PATH_DELIM "\\"
#else
#define PATH_DELIM "/"
#endif
    return globus_common_create_string("%s" PATH_DELIM "%s", dir, filename);
}


int
create_contexts(void)
{
    OM_uint32                           init_maj_stat, maj_stat, min_stat;
    OM_uint32                           ret_flags = GSS_C_GLOBUS_SSL_COMPATIBLE;
    gss_buffer_desc                     init_token;
    gss_buffer_desc                     accept_token;

    init_token.value = accept_token.value = NULL;
    init_token.length = accept_token.length = 0;

    do
    {
        if (init_token.length != 0)
        {
            gss_release_buffer(&min_stat, &init_token);
        }
        init_maj_stat = gss_init_sec_context(
            &min_stat,
            GSS_C_NO_CREDENTIAL,
            &init_ctx,
            GSS_C_NO_NAME,
            GSS_C_NO_OID,
            GSS_C_ANON_FLAG|GSS_C_GLOBUS_SSL_COMPATIBLE,
            0,
            GSS_C_NO_CHANNEL_BINDINGS,
            &accept_token,
            NULL,
            &init_token,
            NULL,
            NULL);

        if (GSS_ERROR(init_maj_stat))
        {
            fprintf(stderr, "# Error initialzing context\n");
            return 1;
        }

        if (accept_token.length != 0)
        {
            gss_release_buffer(&min_stat, &accept_token);
        }

        if (init_token.length != 0)
        {
            maj_stat = gss_accept_sec_context(
                    &min_stat,
                    &accept_ctx,
                    GSS_C_NO_CREDENTIAL,
                    &init_token,
                    GSS_C_NO_CHANNEL_BINDINGS,
                    NULL,
                    NULL,
                    &accept_token,
                    &ret_flags,
                    NULL,
                    NULL);
        }

        if (GSS_ERROR(maj_stat))
        {
            fprintf(stderr, "# Error accepting context\n");
            return 2;
        }
    }
    while (maj_stat & GSS_S_CONTINUE_NEEDED ||
           init_maj_stat & GSS_S_CONTINUE_NEEDED);

    if (GSS_ERROR(maj_stat) || GSS_ERROR(init_maj_stat))
    {
        return 3;
    }
    return 0;
}
/* create_contexts() */

int gridmap_bad_params_test(void)
{
    char *                              globusid = "globusid";
    char *                              userid = "userid";
    int                                 rc;
    char *                              gridmap;
    
    gridmap = create_file_path(gridmap_dir, "grid-mapfile");

    rc = globus_libc_setenv("GRIDMAP", gridmap, 1);

    if (rc != 0)
    {
        fprintf(stderr, "# Error setting GRIDMAP location\n");
        goto out;
    }

    rc = globus_gss_assist_gridmap(NULL, &userid);
    if (rc == GLOBUS_SUCCESS)
    {
        fprintf(stderr, "# Unexpected success: globus_gss_assist_gridmap with null globusid\n");
        rc = 1;
        goto out;
    }

    rc = globus_gss_assist_gridmap(globusid, NULL);
    if (rc == GLOBUS_SUCCESS)
    {
        fprintf(stderr, "# Unexpected success: globus_gss_assist_gridmap with null userid\n");
        rc = 1;
        goto out;
    }
    rc = 0;

out:
    free(gridmap);
    return rc;
}
/* gridmap_bad_params_test() */

int
userok_bad_params_test(void)
{
    char *                              globusid = "globusid";
    char *                              userid = "userid";
    int                                 rc;
    char *                              gridmap;
    
    gridmap = create_file_path(gridmap_dir, "grid-mapfile");

    rc = globus_libc_setenv("GRIDMAP", gridmap, 1);

    if (rc != 0)
    {
        fprintf(stderr, "# Error setting GRIDMAP location\n");
        goto out;
    }

    rc = globus_gss_assist_userok(NULL, userid);
    if (rc == GLOBUS_SUCCESS)
    {
        fprintf(stderr, "# Unexpected success: globus_gss_assist_userok with null globusid\n");
        rc = 1;
        goto out;
    }

    rc = globus_gss_assist_userok(globusid, NULL);
    if (rc == GLOBUS_SUCCESS)
    {
        fprintf(stderr, "# Unexpected success: globus_gss_assist_userok with null userid\n");
        rc = 1;
        goto out;
    }
    rc = 0;

out:
    free(gridmap);
    return rc;
}
/* userok_bad_params_test() */

int
map_local_user_bad_params_test(void)
{
    char *                              globusid;
    char *                              userid = "joe";
    int                                 rc;
    char *                              gridmap;
    
    gridmap = create_file_path(gridmap_dir, "grid-mapfile");

    rc = globus_libc_setenv("GRIDMAP", gridmap, 1);

    if (rc != 0)
    {
        fprintf(stderr, "# Error setting GRIDMAP location\n");
        goto out;
    }

    rc = globus_gss_assist_map_local_user(userid, NULL);
    if (rc == GLOBUS_SUCCESS)
    {
        fprintf(stderr, "# Unexpected success: globus_gss_assist_map_local_user with null globusidp\n");
        rc = 1;
        goto out;
    }

    rc = globus_gss_assist_map_local_user(NULL, &globusid);
    if (rc == GLOBUS_SUCCESS)
    {
        fprintf(stderr, "# Unexpected success: globus_gss_assist_map_local_user with null userid\n");
        rc = 1;
        goto out;
    }
    rc = 0;

out:
    free(gridmap);
    return rc;
}
/* map_local_user_bad_params_test() */

int
lookup_all_globusid_bad_params_test(void)
{
    char *                              username = "joe";
    char **                             dns;
    int                                 dn_count;
    int                                 rc;
    char *                              gridmap;
    
    gridmap = create_file_path(gridmap_dir, "grid-mapfile");

    rc = globus_libc_setenv("GRIDMAP", gridmap, 1);

    if (rc != 0)
    {
        fprintf(stderr, "# Error setting GRIDMAP location\n");
        goto out;
    }

    rc = globus_gss_assist_lookup_all_globusid(NULL, &dns, &dn_count);
    if (rc == GLOBUS_SUCCESS)
    {
        fprintf(stderr, "# Unexpected success: globus_gss_assist_lookup_all_globusid with null username\n");
        rc = 1;
        goto out;
    }

    rc = globus_gss_assist_lookup_all_globusid(username, NULL, &dn_count);
    if (rc == GLOBUS_SUCCESS)
    {
        fprintf(stderr, "# Unexpected success: globus_gss_assist_lookup_all_globusid with null dns\n");
        rc = 2;
        goto out;
    }

    rc = globus_gss_assist_lookup_all_globusid(username, &dns, NULL);
    if (rc == GLOBUS_SUCCESS)
    {
        fprintf(stderr, "# Unexpected success: globus_gss_assist_lookup_all_globusid with null dn_count\n");
        rc = 3;
        goto out;
    }
    rc = 0;

out:
    free(gridmap);
    return rc;
}
/* lookup_all_globusid_bad_params_test() */

int
map_and_authorize_bad_params_test(void)
{
    int                                 rc;
    char *                              service = "service";
    char *                              desired_identity = "id";
    char *                              identity_buffer = "id";
    unsigned int                        identity_buffer_length = 2;
    char *                              gridmap;
    
    gridmap = create_file_path(gridmap_dir, "grid-mapfile");


    rc = globus_libc_setenv("GRIDMAP", gridmap, 1);

    if (rc != 0)
    {
        fprintf(stderr, "# Error setting GRIDMAP location\n");
        goto out;
    }

    rc = globus_gss_assist_map_and_authorize(GSS_C_NO_CONTEXT, service, desired_identity, identity_buffer, identity_buffer_length);
    if (rc == GLOBUS_SUCCESS)
    {
        fprintf(stderr, "# Unexpected success: globus_gss_assist_map_and_authorize with null context\n");
        rc = 1;
        goto out;
    }

    rc = globus_gss_assist_map_and_authorize(accept_ctx, NULL, desired_identity, identity_buffer, identity_buffer_length);
    if (rc == GLOBUS_SUCCESS)
    {
        fprintf(stderr, "# Unexpected success: globus_gss_assist_map_and_authorize with null service\n");
        rc = 2;
        goto out;
    }

    rc = globus_gss_assist_map_and_authorize(accept_ctx, service, NULL, identity_buffer, identity_buffer_length);
    if (rc == GLOBUS_SUCCESS)
    {
        fprintf(stderr, "# Unexpected success: globus_gss_assist_map_and_authorize with null desired_identity\n");
        rc = 3;
        goto out;
    }

    rc = 0;

out:
    free(gridmap);
    return rc;
}
/* map_and_authorize_bad_params_test() */

int
gridmap_test(void)
{
    struct gridmap_lookup_result        tests[] =
    {
        { "gridmap.empty", test_dn, NULL, GLOBUS_FALSE },
        { "gridmap.no-local-uid", test_dn, NULL, GLOBUS_FALSE },
        { "gridmap.no-local-uid2", test_dn, NULL, GLOBUS_FALSE },
        { "grid-mapfile", test_dn, primary_username, GLOBUS_TRUE },
        { "grid-mapfile", wrong_test_dn, NULL, GLOBUS_FALSE }
    };
    char *                              username;
    int                                 i;
    int                                 failed;
    int                                 rc;
    char *                              gridmap;
    

    for (i = 0, failed = 0; i < SIZEOF_ARRAY(tests); i++)
    {
        gridmap = create_file_path(gridmap_dir, tests[i].gridmap);
        rc = globus_libc_setenv("GRIDMAP", gridmap, 1);
        if (rc != 0)
        {
            fprintf(stderr, "# Error setting GRIDMAP location\n");
            failed++;
            continue;
        }

        rc = globus_gss_assist_gridmap(tests[i].dn, &username);
        if (rc != 0 && tests[i].success)
        {
            fprintf(stderr, "# globus_gss_assist_gridmap unexpectedly failed [lookup %s in %s]\n", tests[i].dn, tests[i].gridmap);
            failed++;
            free(gridmap);
            continue;
        }
        else if (rc == 0 && !tests[i].success)
        {
            fprintf(stderr, "# globus_gss_assist_gridmap unexpectedly succeeded [lookup %s in %s]\n", tests[i].dn, tests[i].gridmap);
            failed++;
            free(gridmap);
            continue;
        }
        else if (rc == 0 && strcmp(tests[i].username, username) != 0)
        {
            fprintf(stderr, "# globus_gss_assist_gridmap mapped to wrong name [lookup %s in %s]\nexpected \"%s\" got \"%s\"", tests[i].dn, tests[i].gridmap,
            username, tests[i].username);
            failed++;
            free(gridmap);
            continue;

        }
        if (username != NULL)
        {
            free(username);
            username = NULL;
        }
        free(gridmap);
    }

    return failed;
}
/* gridmap_test() */

int
userok_test(void)
{
    struct gridmap_lookup_result        tests[] =
    {
        { "gridmap.empty", test_dn, primary_username, GLOBUS_FALSE },
        { "gridmap.no-local-uid", test_dn, primary_username, GLOBUS_FALSE },
        { "gridmap.no-local-uid2", test_dn, primary_username, GLOBUS_FALSE },
        { "grid-mapfile", test_dn, primary_username, GLOBUS_TRUE },
        { "grid-mapfile", test_dn, secondary_username[0], GLOBUS_TRUE },
        { "grid-mapfile", test_dn, secondary_username[1], GLOBUS_TRUE },
        { "grid-mapfile", test_dn, wrong_username, GLOBUS_FALSE },
        /* next few are unfortunate */
        { "gridmap.multiple_lines", test_dn, primary_username, GLOBUS_TRUE },
        { "gridmap.multiple_lines", test_dn, secondary_username[0], GLOBUS_FALSE },
        { "gridmap.multiple_lines", test_dn, secondary_username[1], GLOBUS_FALSE },
        { "gridmap.multiple_lines", test_dn, wrong_username, GLOBUS_FALSE },
        { "grid-mapfile", wrong_test_dn, primary_username, GLOBUS_FALSE },
        { "grid-mapfile", wrong_test_dn, secondary_username[0], GLOBUS_FALSE },
        { "grid-mapfile", wrong_test_dn, secondary_username[1], GLOBUS_FALSE }
    };
    int                                 i;
    int                                 failed;
    int                                 rc;

    for (i = 0, failed = 0; i < SIZEOF_ARRAY(tests); i++)
    {
        char *                          gridmap;

        gridmap = create_file_path(gridmap_dir, tests[i].gridmap);
        rc = globus_libc_setenv("GRIDMAP", gridmap, 1);
        if (rc != 0)
        {
            fprintf(stderr, "# Error setting GRIDMAP location\n");
            failed++;
            free(gridmap);
            continue;
        }

        rc = globus_gss_assist_userok(tests[i].dn, tests[i].username);
        if (rc != 0 && tests[i].success)
        {
            fprintf(stderr, "# globus_gss_assist_userok unexpectedly failed [userok %s for %s in %s]\n", tests[i].username, tests[i].dn, tests[i].gridmap);
            failed++;
            free(gridmap);
            continue;
        }
        else if (rc == 0 && !tests[i].success)
        {
            fprintf(stderr, "# globus_gss_assist_userok unexpectedly succeeded [userok %s for %s in %s]\n", tests[i].username, tests[i].dn, tests[i].gridmap);
            failed++;
            free(gridmap);
            continue;
        }
        free(gridmap);
    }

    return failed;
}
/* userok_test() */

int
map_local_user_test(void)
{
    struct gridmap_lookup_result        tests[] =
    {
        { "gridmap.empty", test_dn, primary_username, GLOBUS_FALSE },
        { "gridmap.no-local-uid", test_dn, primary_username, GLOBUS_FALSE },
        { "gridmap.no-local-uid2", test_dn, primary_username, GLOBUS_FALSE },
        { "grid-mapfile", test_dn, primary_username, GLOBUS_TRUE },
        { "grid-mapfile", test_dn, secondary_username[0], GLOBUS_TRUE },
        { "grid-mapfile", test_dn, secondary_username[1], GLOBUS_TRUE },
        { "grid-mapfile", test_dn, wrong_username, GLOBUS_FALSE },
        { "gridmap.multiple_lines", test_dn, primary_username, GLOBUS_TRUE },
        { "gridmap.multiple_lines", test_dn, secondary_username[0], GLOBUS_TRUE },
        { "gridmap.multiple_lines", test_dn, secondary_username[1], GLOBUS_TRUE },
        { "gridmap.multiple_lines", test_dn, wrong_username, GLOBUS_FALSE }
    };
    char *                              dn = NULL;
    int                                 i;
    int                                 failed;
    int                                 rc;

    for (i = 0, failed = 0; i < SIZEOF_ARRAY(tests); i++)
    {
        char *                          gridmap;

        gridmap = create_file_path(gridmap_dir, tests[i].gridmap);
        rc = globus_libc_setenv("GRIDMAP", gridmap, 1);
        if (rc != 0)
        {
            fprintf(stderr, "# Error setting GRIDMAP location\n");
            failed++;
            free(gridmap);
            continue;
        }

        rc = globus_gss_assist_map_local_user(tests[i].username, &dn);
        if (rc != 0 && tests[i].success)
        {
            fprintf(stderr, "# globus_gss_assist_map_local_user unexpectedly failed [map %s in %s]\n", tests[i].username, tests[i].gridmap);
            failed++;
            free(gridmap);
            continue;
        }
        else if (rc == 0 && !tests[i].success)
        {
            fprintf(stderr, "# globus_gss_assist_map_local_user unexpectedly succeeded [map %s in %s]\n", tests[i].username, tests[i].gridmap);
            failed++;
            free(gridmap);
            continue;
        }
        else if (rc == 0 && strcmp(tests[i].dn, dn) != 0)
        {
            fprintf(stderr, "# globus_gss_assist_map_local_user mapped %s to %s using %s [expected %s]\n", tests[i].username, dn, tests[i].gridmap, tests[i].dn);
            failed++;
            free(gridmap);
            continue;
        }
        if (dn != NULL)
        {
            free(dn);
            dn = NULL;
        }
        free(gridmap);
    }

    return failed;
}
/* map_local_user_test() */

static
int
lookup_all_globusid_test(void)
{
    globus_result_t                     result;
    char **                             dns = NULL;
    int                                 i;
    int                                 rc;
    int                                 failed;
    int                                 dn_count;
    struct gridmap_all_globusid_result  tests[] =
    {
        { "grid-mapfile", primary_username, test_dn, NULL, GLOBUS_TRUE },
        { "grid-mapfile", secondary_username[0],  test_dn, NULL, GLOBUS_TRUE },
        { "grid-mapfile", secondary_username[1],  test_dn, NULL, GLOBUS_TRUE },
        { "grid-mapfile", wrong_username, NULL, NULL, GLOBUS_TRUE },
        { "gridmap.empty", primary_username, NULL, NULL, GLOBUS_TRUE },
        { "gridmap.empty", secondary_username[0], NULL, NULL, GLOBUS_TRUE },
        { "gridmap.empty", secondary_username[1], NULL, NULL, GLOBUS_TRUE },
        { "gridmap.multiple-dns", primary_username, test_dn, test_dn2, GLOBUS_TRUE },
        { "gridmap.multiple-dns", secondary_username[0], test_dn, test_dn2, GLOBUS_TRUE },
        { "gridmap.multiple-dns", secondary_username[1], test_dn, test_dn2, GLOBUS_TRUE },
        { "gridmap.multiple-dns", wrong_username, NULL, NULL, GLOBUS_TRUE },
        { "gridmap.multiple_lines", primary_username, test_dn, NULL, GLOBUS_TRUE },
        { "gridmap.multiple_lines", secondary_username[0],  test_dn, NULL, GLOBUS_TRUE },
        { "gridmap.multiple_lines", secondary_username[1],  test_dn, NULL, GLOBUS_TRUE },
        { "gridmap.multiple_lines", wrong_username, NULL, NULL, GLOBUS_TRUE },
        { "gridmap.no-local-uid", primary_username, NULL, NULL, GLOBUS_TRUE },
        { "gridmap.no-local-uid2", primary_username, NULL, NULL, GLOBUS_TRUE },
    };

    for (i = 0, failed = 0; i < SIZEOF_ARRAY(tests); i++)
    {
        char *                          gridmap;

        gridmap = create_file_path(gridmap_dir, tests[i].gridmap);
        rc = globus_libc_setenv("GRIDMAP", gridmap, 1);
        if (rc != 0)
        {
            fprintf(stderr, "# Error setting GRIDMAP location\n");
            failed++;
            free(gridmap);
            continue;
        }
        if (dns != NULL)
        {
            GlobusGssAssistFreeDNArray(dns);
            dns = NULL;
        }

        result = globus_gss_assist_lookup_all_globusid(
                tests[i].username,
                &dns,
                &dn_count);
        if (result != GLOBUS_SUCCESS && tests[i].success)
        {
            fprintf(stderr, "# globus_gss_assist_lookup_all_globusid unexpectedly failed [map %s in %s]\n", tests[i].username, tests[i].gridmap);
            failed++;
            free(gridmap);
            continue;
        }
        else if (result == GLOBUS_SUCCESS && !tests[i].success)
        {
            fprintf(stderr, "# globus_gss_assist_lookup_all_globusid unexpectedly succeeded [map %s in %s]\n", tests[i].username, tests[i].gridmap);
            failed++;
            free(gridmap);
            continue;
        }
        else if (result == GLOBUS_SUCCESS)
        {
            if (tests[i].test_dn1 != NULL &&
                tests[i].test_dn2 != NULL &&
                dn_count != 2)
            {
                fprintf(stderr, "# globus_gss_assist_lookup_all_globusid returned %d DNS, expected 2 when mapping %s in %s\n", dn_count, tests[i].username, tests[i].gridmap);
                failed++;
                free(gridmap);
                continue;
            }
            else if (tests[i].test_dn1 != NULL &&
                     tests[i].test_dn2 == NULL &&
                     dn_count != 1)
            {
                fprintf(stderr, "# globus_gss_assist_lookup_all_globusid returned %d DNS, expected 1 when mapping %s in %s\n", dn_count, tests[i].username, tests[i].gridmap);
                failed++;
                free(gridmap);
                continue;
            }
            else if (tests[i].test_dn1 == NULL &&
                    tests[i].test_dn2 == NULL &&
                    dn_count != 0)
            {
                fprintf(stderr, "# globus_gss_assist_lookup_all_globusid returned %d DNS, expected 0\n", dn_count);
                failed++;
                free(gridmap);
                continue;
            }
            if (tests[i].test_dn1 != NULL && tests[i].test_dn2 != NULL)
            {
                if ((strcmp(tests[i].test_dn1, dns[0]) == 0 &&
                      strcmp(tests[i].test_dn2, dns[1]) == 0) ||
                     (strcmp(tests[i].test_dn1, dns[1]) == 0 &&
                      strcmp(tests[i].test_dn2, dns[0]) == 0))
                {
                    /* Good */
                    free(gridmap);
                    continue;
                }
                else
                {
                    fprintf(stderr, "# expected globus_gss_assist_lookup_all_globusid to return %s and %s, got %s and %s\n",
                            tests[i].test_dn1, test_dn2, dns[0], dns[1]);
                    failed++;
                    free(gridmap);
                    continue;
                }
            }
            else if (tests[i].test_dn1 != NULL && tests[i].test_dn2 == NULL)
            {
                if (strcmp(tests[i].test_dn1, dns[0]) == 0)
                {
                    /* Good */
                    free(gridmap);
                    continue;
                }
                else
                {
                    fprintf(stderr, "# expected globus_gss_assist_lookup_all_globusid to return %s, got %s",
                            tests[i].test_dn1, dns[0]);
                }
            }
        }
        free(gridmap);
    }
    if (dns != NULL)
    {
        GlobusGssAssistFreeDNArray(dns);
        dns = NULL;
    }

    return failed;
}
/* lookup_all_globusid_test() */

int
long_line_test(void)
{
    int                                 i;
    int                                 failed = 0;
    int                                 rc;
    char                                localname[7];
    char *                              gridmap;

    gridmap = create_file_path(gridmap_dir, "gridmap.long_line");

    rc = globus_libc_setenv("GRIDMAP", gridmap, 1);
    if (rc != 0)
    {
        fprintf(stderr, "# Error setting GRIDMAP location\n");
        failed++;
        goto setenv_failed;
    }

    for (i = 1, failed = 0; i <= 1000; i++)
    {
        sprintf(localname, "jd%d", i);

        rc = globus_gss_assist_userok(test_dn, localname);
        if (rc != 0)
        {
            fprintf(stderr, "# globus_gss_assist_userok unexpectedly failed [userok %s for %s in %s]\n", localname, test_dn, gridmap);
            failed++;
            continue;
        }
    }
    for (i = 1001; i <= 2000; i++)
    {
        sprintf(localname, "jd%d", i);

        rc = globus_gss_assist_userok(test_dn, localname);
        if (rc == 0)
        {
            fprintf(stderr, "# globus_gss_assist_userok unexpectedly succeeded [userok %s for %s in %s]\n", localname, test_dn, gridmap);
            failed++;
            continue;
        }
    }
setenv_failed:
    free(gridmap);
    return failed;
}
/* userok_test() */

int
blank_line_test(void)
{
    int                                 failed = 0;
    int                                 rc;
    char *                              gridmap;

    gridmap = create_file_path(gridmap_dir, "gridmap.blank_line");

    rc = globus_libc_setenv("GRIDMAP", gridmap, 1);
    if (rc != 0)
    {
        fprintf(stderr, "# Error setting GRIDMAP location\n");
        failed++;
        goto setenv_failed;
    }

    rc = globus_gss_assist_userok(test_dn, "jdoe");
    if (rc != 0)
    {
        fprintf(stderr, "# globus_gss_assist_userok unexpectedly failed [userok %s for %s in %s]\n", "jdoe", test_dn, gridmap);
        failed++;
    }

setenv_failed:
    free(gridmap);
    return failed;
}
/* blank_line_test() */


int main(int argc, char * argv[])
{
    test_case_t                         tests[] =
    {
        TEST_CASE(gridmap_bad_params_test),
        TEST_CASE(userok_bad_params_test),
        TEST_CASE(map_local_user_bad_params_test),
        TEST_CASE(lookup_all_globusid_bad_params_test),
        TEST_CASE(map_and_authorize_bad_params_test),
        TEST_CASE(gridmap_test),
        TEST_CASE(userok_test),
        TEST_CASE(map_local_user_test),
        TEST_CASE(lookup_all_globusid_test),
        TEST_CASE(long_line_test),
        TEST_CASE(blank_line_test)
    };
    int                                 i;
    int                                 failed = 0;
    int                                 rc;

    setbuf(stdout, NULL);
    setbuf(stderr, NULL);

    gridmap_dir = getenv("TEST_GRIDMAP_DIR");
    if (gridmap_dir == NULL)
    {
        gridmap_dir = ".";
    }
    rc = globus_module_activate(GLOBUS_GSI_GSS_ASSIST_MODULE);
    if (rc != 0)
    {
        return -1;
    }

    rc = create_contexts();
    if (rc != 0)
    {
        return -2;
    }

    printf("1..%d\n", (int) SIZEOF_ARRAY(tests));

    for (i = 0; i < SIZEOF_ARRAY(tests); i++)
    {
        rc = (tests[i].func)();

        printf("%s %s\n", (rc==0) ? "ok" : "not ok", tests[i].name);

        failed += (rc != 0);
    }

    return failed;
}
