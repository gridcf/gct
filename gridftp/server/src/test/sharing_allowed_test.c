#include <stdio.h>
#include <stdbool.h>
#include <pwd.h>
#include <grp.h>

#include "globus_common.h"
#include "globus_gridftp_server.h"
#include "globus_preload.h"

#include "globus_i_gfs_data.c"

extern int globus_i_gfs_config_init();

typedef struct
{
    char *                              test_name;
    char *                              sharing_users_allow;
    char *                              sharing_users_deny;
    char *                              sharing_groups_allow;
    char *                              sharing_groups_deny;
    char *                              sharing_user;
    bool                                expected_result;
}
sharing_allow_test_case_t;

#define TEST_ASSERT(x) \
    if (!(x)) { \
        fprintf(stderr, "# Failed %s: %s\n", test_case->test_name, #x); \
        test_result = 1; \
        goto test_cleanup; \
    }

int 
test_sharing_allowed(const sharing_allow_test_case_t *test_case)
{
    char *                              argv[9] = {"globus-gridftp-server"};
    int                                 argc = 1;
    int                                 test_result = 0;
    bool                                allowed = false;
    int                                 rc;
    globus_l_gfs_data_session_t         session_handle = 
    {
        .username = test_case->sharing_user,
        .gid_count = getgroups(0, NULL) + 1,
    };

    session_handle.gid_array = malloc(session_handle.gid_count * sizeof(gid_t));
    TEST_ASSERT(session_handle.gid_array != NULL);

    rc = getgroups(session_handle.gid_count, session_handle.gid_array);
    if (rc < session_handle.gid_count)
    {
        session_handle.gid_array[session_handle.gid_count-1] = getegid();
    }

    session_handle.gid = getegid();

    if (test_case->sharing_users_allow)
    {
        argv[argc++] = "-sharing-users-allow";
        argv[argc++] = test_case->sharing_users_allow;
    }
    if (test_case->sharing_users_deny)
    {
        argv[argc++] = "-sharing-users-deny";
        argv[argc++] = test_case->sharing_users_deny;
    }
    if (test_case->sharing_groups_allow)
    {
        argv[argc++] = "-sharing-groups-allow";
        argv[argc++] = test_case->sharing_groups_allow;
    }
    if (test_case->sharing_groups_deny)
    {
        argv[argc++] = "-sharing-groups-deny";
        argv[argc++] = test_case->sharing_groups_deny;
    }
    assert(argc <= (sizeof(argv) / sizeof(*argv)));

    rc = globus_i_gfs_config_init(argc, argv, true);
    TEST_ASSERT(rc == 0);

    allowed = globus_l_gfs_data_check_sharing_allowed(&session_handle);
    TEST_ASSERT(allowed == test_case->expected_result);

test_cleanup:
    free(session_handle.gid_array);
    return test_result;
}

int main()
{
    uid_t                               my_uid;
    gid_t                               my_groups[NGROUPS_MAX];
    gid_t                               my_default_gid;
    char *                              my_username;
    char *                              my_default_group;
    char *                              my_other_group;
    struct passwd                      *pwent;
    struct group                       *grent;
    int                                 rc;
    int failed = 0;
    bool                                skip_other_group = false;
    globus_module_descriptor_t         *modules[] = {
        GLOBUS_COMMON_MODULE,
        GLOBUS_GRIDFTP_SERVER_MODULE,
        NULL
    };

    LTDL_SET_PRELOADED_SYMBOLS();

    my_uid = getuid();
    pwent = getpwuid(my_uid);
    if (pwent == NULL)
    {
        fprintf(stderr, "Unable to determine username\n");
        exit(99);
    }
    my_username = strdup(pwent->pw_name);
    if (my_username == NULL)
    {
        fprintf(stderr, "Error allocating user name: %s\n",
                strerror(errno));
        exit(99);
    }

    rc = getgroups(NGROUPS_MAX, my_groups);
    if (rc<0)
    {
        fprintf(stderr, "Error: getgroups(NGROUPS_MAX, my_groups) failed: %s\n",
                strerror(errno));
        exit(99);
    }
    my_default_gid = (rc>0 ? my_groups[0] : getgid());
    grent = getgrgid(my_default_gid);
    if (grent == NULL)
    {
        fprintf(stderr,
                "Unable to determine my default group name (gid %d)\n",
                my_default_gid);
        exit(99);
    }
    my_default_group = strdup(grent->gr_name);
    if (my_default_group == NULL)
    {
        fprintf(stderr, "Error allocating default group name: %s\n",
                strerror(errno));
        exit(99);
    }
    if (rc < 2)
    {
        my_other_group = "";
        skip_other_group = true;
    }
    else
    {
        grent = getgrgid(my_groups[1]);
        if (grent == NULL)
        {
            fprintf(stderr, "Unable to determine a secondary group name\n");
            exit(99);
        }
        my_other_group = strdup(grent->gr_name);
        if (my_other_group == NULL)
        {
            fprintf(stderr, "Error allocating secondary group name: %s\n",
                    strerror(errno));
            exit(99);
        }
    }
    rc = globus_module_activate_array(modules, NULL);
    if (rc != GLOBUS_SUCCESS)
    {
        fprintf(stderr, "Error activating modules: %d\n", rc);
        exit(99);
    }
    {
        char *good_user = my_username;
        char *good_user_initial_list = 
                globus_common_create_string("%s,:bad:,:other:,:another:",
                        my_username);
        char *good_user_mid_list = 
                globus_common_create_string(":bad:,%s,:other:,:another:",
                        my_username);
        char *good_user_end_list = 
                globus_common_create_string(":bad:,:other:,:another:,%s",
                        my_username);
        char *bad = ":bad:";
        char *bad_list = ":bad:,:other:,:another:";
        char *good_group = my_default_group;
        char *good_group_initial_list = 
                globus_common_create_string("%s,:bad:,:other:,:another:",
                        my_default_group);
        char *good_group_mid_list = 
                globus_common_create_string(":bad:,%s,:other:,:another:",
                        my_default_group);
        char *good_group_end_list = 
                globus_common_create_string(":bad:,:other:,:another:,%s",
                        my_default_group);
        char *good_other_group = my_other_group;
        char *good_other_group_initial_list = 
                globus_common_create_string("%s,:bad:,:other:,:another:",
                        my_other_group);
        char *good_other_group_mid_list = 
                globus_common_create_string(":bad:,%s,:other:,:another:",
                        my_other_group);
        char *good_other_group_end_list = 
                globus_common_create_string(":bad:,:other:,:another:,%s",
                        my_other_group);
        char *empty_list = "";
        char *empty_comma_list = ",";

        sharing_allow_test_case_t test_cases[] =
        {
            {
                .test_name = "null-allowed-and-denied",
                .sharing_users_allow = NULL,
                .sharing_users_deny = NULL,
                .sharing_groups_allow = NULL,
                .sharing_groups_deny = NULL,
                .sharing_user = my_username,
                .expected_result = true
            },
            {
                .test_name = "not-allowed-or-denied",
                .sharing_users_allow = bad_list,
                .sharing_users_deny = bad_list,
                .sharing_groups_allow = NULL,
                .sharing_groups_deny = NULL,
                .sharing_user = my_username,
                .expected_result = false
            },
            {
                .test_name = "user-allowed",
                .sharing_users_allow = good_user,
                .sharing_users_deny = NULL,
                .sharing_groups_allow = NULL,
                .sharing_groups_deny = NULL,
                .sharing_user = my_username,
                .expected_result = true
            },
            {
                .test_name = "user-allowed-initial-list",
                .sharing_users_allow = good_user_initial_list,
                .sharing_users_deny = NULL,
                .sharing_groups_allow = NULL,
                .sharing_groups_deny = NULL,
                .sharing_user = my_username,
                .expected_result = true
            },
            {
                .test_name = "user-allowed-mid-list",
                .sharing_users_allow = good_user_mid_list,
                .sharing_users_deny = NULL,
                .sharing_groups_allow = NULL,
                .sharing_groups_deny = NULL,
                .sharing_user = my_username,
                .expected_result = true
            },
            {
                .test_name = "user-allowed-end-list",
                .sharing_users_allow = good_user_end_list,
                .sharing_users_deny = NULL,
                .sharing_groups_allow = NULL,
                .sharing_groups_deny = NULL,
                .sharing_user = my_username,
                .expected_result = true
            },
            {
                .test_name = "user-allowed-not-denied",
                .sharing_users_allow = good_user,
                .sharing_users_deny = bad_list,
                .sharing_groups_allow = NULL,
                .sharing_groups_deny = NULL,
                .sharing_user = my_username,
                .expected_result = true
            },
            {
                .test_name = "user-allowed-initial-list-not-denied",
                .sharing_users_allow = good_user_initial_list,
                .sharing_users_deny = bad_list,
                .sharing_groups_allow = NULL,
                .sharing_groups_deny = NULL,
                .sharing_user = my_username,
                .expected_result = true
            },
            {
                .test_name = "user-allowed-mid-list-not-denied",
                .sharing_users_allow = good_user_mid_list,
                .sharing_users_deny = bad_list,
                .sharing_groups_allow = NULL,
                .sharing_groups_deny = NULL,
                .sharing_user = my_username,
                .expected_result = true
            },
            {
                .test_name = "user-allowed-end-list-not-denied",
                .sharing_users_allow = good_user_end_list,
                .sharing_users_deny = bad_list,
                .sharing_groups_allow = NULL,
                .sharing_groups_deny = NULL,
                .sharing_user = my_username,
                .expected_result = true
            },
            {
                .test_name = "not-in-allowed",
                .sharing_users_allow = bad,
                .sharing_users_deny = NULL,
                .sharing_groups_allow = NULL,
                .sharing_groups_deny = NULL,
                .sharing_user = my_username,
                .expected_result = false
            },
            {
                .test_name = "not-in-allowed-list",
                .sharing_users_allow = bad_list,
                .sharing_users_deny = NULL,
                .sharing_groups_allow = NULL,
                .sharing_groups_deny = NULL,
                .sharing_user = my_username,
                .expected_result = false
            },
            {
                .test_name = "in-denied-user",
                .sharing_users_allow = NULL,
                .sharing_users_deny = good_user,
                .sharing_groups_allow = NULL,
                .sharing_groups_deny = NULL,
                .sharing_user = my_username,
                .expected_result = false
            },
            {
                .test_name = "in-denied-user-initial-list",
                .sharing_users_allow = NULL,
                .sharing_users_deny = good_user_initial_list,
                .sharing_groups_allow = NULL,
                .sharing_groups_deny = NULL,
                .sharing_user = my_username,
                .expected_result = false
            },
            {
                .test_name = "in-denied-user-mid-list",
                .sharing_users_allow = NULL,
                .sharing_users_deny = good_user_mid_list,
                .sharing_groups_allow = NULL,
                .sharing_groups_deny = NULL,
                .sharing_user = my_username,
                .expected_result = false
            },
            {
                .test_name = "in-denied-user-end-list",
                .sharing_users_allow = NULL,
                .sharing_users_deny = good_user_end_list,
                .sharing_groups_allow = NULL,
                .sharing_groups_deny = NULL,
                .sharing_user = my_username,
                .expected_result = false
            },
            {
                .test_name = "in-denied-and-allowed-user",
                .sharing_users_allow = good_user,
                .sharing_users_deny = good_user,
                .sharing_groups_allow = NULL,
                .sharing_groups_deny = NULL,
                .sharing_user = my_username,
                .expected_result = false
            },
            {
                .test_name = "no-user-lists-allowed-group",
                .sharing_users_allow = NULL,
                .sharing_users_deny = NULL,
                .sharing_groups_allow = good_group,
                .sharing_groups_deny = NULL,
                .sharing_user = my_username,
                .expected_result = true
            },
            {
                .test_name = "no-user-lists-allowed-group-initial-list",
                .sharing_users_allow = NULL,
                .sharing_users_deny = NULL,
                .sharing_groups_allow = good_group_initial_list,
                .sharing_groups_deny = NULL,
                .sharing_user = my_username,
                .expected_result = true
            },
            {
                .test_name = "no-user-lists-allowed-group-mid-list",
                .sharing_users_allow = NULL,
                .sharing_users_deny = NULL,
                .sharing_groups_allow = good_group_mid_list,
                .sharing_groups_deny = NULL,
                .sharing_user = my_username,
                .expected_result = true
            },
            {
                .test_name = "no-user-lists-allowed-group-end-list",
                .sharing_users_allow = NULL,
                .sharing_users_deny = NULL,
                .sharing_groups_allow = good_group_end_list,
                .sharing_groups_deny = NULL,
                .sharing_user = my_username,
                .expected_result = true
            },
            {
                .test_name = "no-user-lists-deny-group",
                .sharing_users_allow = NULL,
                .sharing_users_deny = NULL,
                .sharing_groups_allow = NULL,
                .sharing_groups_deny = good_group,
                .sharing_user = my_username,
                .expected_result = false
            },
            {
                .test_name = "no-user-lists-deny-group-initial-list",
                .sharing_users_allow = NULL,
                .sharing_users_deny = NULL,
                .sharing_groups_allow = NULL,
                .sharing_groups_deny = good_group_initial_list,
                .sharing_user = my_username,
                .expected_result = false
            },
            {
                .test_name = "no-user-lists-deny-group-mid-list",
                .sharing_users_allow = NULL,
                .sharing_users_deny = NULL,
                .sharing_groups_allow = NULL,
                .sharing_groups_deny = good_group_mid_list,
                .sharing_user = my_username,
                .expected_result = false
            },
            {
                .test_name = "no-user-lists-deny-group-end-list",
                .sharing_users_allow = NULL,
                .sharing_users_deny = NULL,
                .sharing_groups_allow = NULL,
                .sharing_groups_deny = good_group_end_list,
                .sharing_user = my_username,
                .expected_result = false
            },
            {
                .test_name = "no-user-lists-allowed-and-denied-group",
                .sharing_users_allow = NULL,
                .sharing_users_deny = NULL,
                .sharing_groups_allow = good_group,
                .sharing_groups_deny = good_group,
                .sharing_user = my_username,
                .expected_result = false
            },
            {
                .test_name = "not-in-user-allowed-in-allowed-group",
                .sharing_users_allow = bad,
                .sharing_users_deny = NULL,
                .sharing_groups_allow = good_group,
                .sharing_groups_deny = NULL,
                .sharing_user = my_username,
                .expected_result = true
            },
            {
                .test_name = "not-in-user-allowed-or-denied-in-allowed-group",
                .sharing_users_allow = bad,
                .sharing_users_deny = bad,
                .sharing_groups_allow = good_group,
                .sharing_groups_deny = NULL,
                .sharing_user = my_username,
                .expected_result = true
            },
            {
                .test_name = "not-in-user-allowed-or-denied-in-allowed-group-initial-list",
                .sharing_users_allow = bad,
                .sharing_users_deny = bad,
                .sharing_groups_allow = good_group_initial_list,
                .sharing_groups_deny = NULL,
                .sharing_user = my_username,
                .expected_result = true
            },
            {
                .test_name = "not-in-user-allowed-or-denied-in-allowed-group-mid-list",
                .sharing_users_allow = bad,
                .sharing_users_deny = bad,
                .sharing_groups_allow = good_group_mid_list,
                .sharing_groups_deny = NULL,
                .sharing_user = my_username,
                .expected_result = true
            },
            {
                .test_name = "not-in-user-allowed-or-denied-in-allowed-group-end-list",
                .sharing_users_allow = bad,
                .sharing_users_deny = bad,
                .sharing_groups_allow = good_group_end_list,
                .sharing_groups_deny = NULL,
                .sharing_user = my_username,
                .expected_result = true
            },
            {
                .test_name = "not-in-user-allowed-or-denied-in-denied-group",
                .sharing_users_allow = bad,
                .sharing_users_deny = bad,
                .sharing_groups_allow = bad,
                .sharing_groups_deny = good_group,
                .sharing_user = my_username,
                .expected_result = false
            },
            {
                .test_name = "no-user-lists-allowed-other-group",
                .sharing_users_allow = NULL,
                .sharing_users_deny = NULL,
                .sharing_groups_allow = good_other_group,
                .sharing_groups_deny = NULL,
                .sharing_user = my_username,
                .expected_result = true
            },
            {
                .test_name = "not-in-user-lists-allowed-group-initial-list",
                .sharing_users_allow = bad,
                .sharing_users_deny = NULL,
                .sharing_groups_allow = good_group_initial_list,
                .sharing_groups_deny = NULL,
                .sharing_user = my_username,
                .expected_result = true
            },
            {
                .test_name = "not-in-user-lists-allowed-group-mid-list",
                .sharing_users_allow = bad,
                .sharing_users_deny = NULL,
                .sharing_groups_allow = good_group_mid_list,
                .sharing_groups_deny = NULL,
                .sharing_user = my_username,
                .expected_result = true
            },
            {
                .test_name = "not-in-user-lists-allowed-group-end-list",
                .sharing_users_allow = bad,
                .sharing_users_deny = NULL,
                .sharing_groups_allow = good_group_end_list,
                .sharing_groups_deny = NULL,
                .sharing_user = my_username,
                .expected_result = true
            },
            {
                .test_name = "not-in-user-lists-allowed-other-group-initial-list",
                .sharing_users_allow = bad,
                .sharing_users_deny = NULL,
                .sharing_groups_allow = good_other_group_initial_list,
                .sharing_groups_deny = NULL,
                .sharing_user = my_username,
                .expected_result = true
            },
            {
                .test_name = "not-in-user-lists-allowed-other-group-mid-list",
                .sharing_users_allow = bad,
                .sharing_users_deny = NULL,
                .sharing_groups_allow = good_other_group_mid_list,
                .sharing_groups_deny = NULL,
                .sharing_user = my_username,
                .expected_result = true
            },
            {
                .test_name = "not-in-user-lists-allowed-other-group-end-list",
                .sharing_users_allow = bad,
                .sharing_users_deny = NULL,
                .sharing_groups_allow = good_other_group_end_list,
                .sharing_groups_deny = NULL,
                .sharing_user = my_username,
                .expected_result = true
            },
            {
                .test_name = "no-user-lists-deny-other-group",
                .sharing_users_allow = NULL,
                .sharing_users_deny = NULL,
                .sharing_groups_allow = NULL,
                .sharing_groups_deny = good_other_group,
                .sharing_user = my_username,
                .expected_result = false
            },
            {
                .test_name = "no-user-lists-deny-other-group-initial-list",
                .sharing_users_allow = NULL,
                .sharing_users_deny = NULL,
                .sharing_groups_allow = NULL,
                .sharing_groups_deny = good_other_group_initial_list,
                .sharing_user = my_username,
                .expected_result = false
            },
            {
                .test_name = "no-user-lists-deny-other-group-mid-list",
                .sharing_users_allow = NULL,
                .sharing_users_deny = NULL,
                .sharing_groups_allow = NULL,
                .sharing_groups_deny = good_other_group_mid_list,
                .sharing_user = my_username,
                .expected_result = false
            },
            {
                .test_name = "no-user-lists-deny-other-group-end-list",
                .sharing_users_allow = NULL,
                .sharing_users_deny = NULL,
                .sharing_groups_allow = NULL,
                .sharing_groups_deny = good_other_group_end_list,
                .sharing_user = my_username,
                .expected_result = false
            },
            {
                .test_name = "no-user-lists-allowed-and-denied-other-group",
                .sharing_users_allow = NULL,
                .sharing_users_deny = NULL,
                .sharing_groups_allow = good_other_group,
                .sharing_groups_deny = good_other_group,
                .sharing_user = my_username,
                .expected_result = false
            },
            {
                .test_name = "not-in-user-allowed-in-allowed-other-group",
                .sharing_users_allow = bad,
                .sharing_users_deny = NULL,
                .sharing_groups_allow = good_other_group,
                .sharing_groups_deny = NULL,
                .sharing_user = my_username,
                .expected_result = true
            },
            {
                .test_name = "not-in-user-allowed-or-denied-in-allowed-other-group",
                .sharing_users_allow = bad,
                .sharing_users_deny = bad,
                .sharing_groups_allow = good_other_group,
                .sharing_groups_deny = NULL,
                .sharing_user = my_username,
                .expected_result = true
            },
            {
                .test_name = "not-in-user-allowed-or-denied-in-denied-other-group",
                .sharing_users_allow = bad,
                .sharing_users_deny = bad,
                .sharing_groups_allow = bad,
                .sharing_groups_deny = good_other_group,
                .sharing_user = my_username,
                .expected_result = false
            },
            {
                .test_name = "not-in-user-or-group-allowed-or-denied",
                .sharing_users_allow = bad,
                .sharing_users_deny = bad,
                .sharing_groups_allow = bad,
                .sharing_groups_deny = bad,
                .sharing_user = my_username,
                .expected_result = false
            },
            {
                .test_name = "not-in-any-list-empty-users-allowed",
                .sharing_users_allow = empty_list,
                .sharing_users_deny = bad,
                .sharing_groups_allow = bad,
                .sharing_groups_deny = bad,
                .sharing_user = my_username,
                .expected_result = false
            },
            {
                .test_name = "not-in-any-list-empty-users-denied",
                .sharing_users_allow = bad,
                .sharing_users_deny = empty_list,
                .sharing_groups_allow = bad,
                .sharing_groups_deny = bad,
                .sharing_user = my_username,
                .expected_result = false
            },
            {
                .test_name = "not-in-any-list-empty-groups-allowed",
                .sharing_users_allow = bad,
                .sharing_users_deny = bad,
                .sharing_groups_allow = empty_list,
                .sharing_groups_deny = bad,
                .sharing_user = my_username,
                .expected_result = false
            },
            {
                .test_name = "not-in-any-list-empty-groups-denied",
                .sharing_users_allow = bad,
                .sharing_users_deny = bad,
                .sharing_groups_allow = bad,
                .sharing_groups_deny = empty_list,
                .sharing_user = my_username,
                .expected_result = false
            },
            {
                .test_name = "not-in-any-list-empty-comma-users-allowed",
                .sharing_users_allow = empty_comma_list,
                .sharing_users_deny = bad,
                .sharing_groups_allow = bad,
                .sharing_groups_deny = bad,
                .sharing_user = my_username,
                .expected_result = false
            },
            {
                .test_name = "not-in-any-list-empty-comma-users-denied",
                .sharing_users_allow = bad,
                .sharing_users_deny = empty_comma_list,
                .sharing_groups_allow = bad,
                .sharing_groups_deny = bad,
                .sharing_user = my_username,
                .expected_result = false
            },
            {
                .test_name = "not-in-any-list-empty-comma-groups-allowed",
                .sharing_users_allow = bad,
                .sharing_users_deny = bad,
                .sharing_groups_allow = empty_comma_list,
                .sharing_groups_deny = bad,
                .sharing_user = my_username,
                .expected_result = false
            },
            {
                .test_name = "not-in-any-list-empty-comma-groups-denied",
                .sharing_users_allow = bad,
                .sharing_users_deny = bad,
                .sharing_groups_allow = bad,
                .sharing_groups_deny = empty_comma_list,
                .sharing_user = my_username,
                .expected_result = false
            },

        };
        printf("1..%d\n", (int) (sizeof(test_cases)/sizeof(*test_cases)));

        for (int i = 0; i < sizeof(test_cases)/sizeof(*test_cases); i++)
        {
            if (skip_other_group &&
                strstr(test_cases[i].test_name, "other-group") != NULL)
            {
                printf("ok %d # SKIP no supplementary groups\n", i+1);
                continue;
            }
            printf("# u='%s' ua='%s' ud='%s' ga='%s' gd='%s' %s\n",
                    test_cases[i].sharing_user
                            ? test_cases[i].sharing_user : "NULL",
                    test_cases[i].sharing_users_allow
                            ? test_cases[i].sharing_users_allow: "NULL",
                    test_cases[i].sharing_users_deny
                            ? test_cases[i].sharing_users_deny : "NULL",
                    test_cases[i].sharing_groups_allow
                            ? test_cases[i].sharing_groups_allow : "NULL",
                    test_cases[i].sharing_groups_deny
                            ? test_cases[i].sharing_groups_deny : "NULL",
                    test_cases[i].expected_result
                            ? "true" : "false");
            rc = test_sharing_allowed(&test_cases[i]);
            if (rc != 0)
            {
                failed++;
                printf("not ");
            }
            printf("ok %d - %s\n",
                    i+1,
                    test_cases[i].test_name);
        }
    }
    globus_module_deactivate_all();
    return failed;
}
