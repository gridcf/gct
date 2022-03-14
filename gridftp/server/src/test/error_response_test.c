#include "globus_gridftp_server.h"
#include "globus_i_gridftp_server.h"
#include <stdbool.h>

bool
test_default_message(void)
{
    globus_result_t                     result = GLOBUS_SUCCESS;
    char                               *message = NULL;
    bool                                ok = false;

    errno = ENOMEM;
    result = GlobusGFSErrorMemory("foo");

    message = globus_object_printable_to_string(
            globus_error_peek(result));

    fprintf(stderr, "# %s %s\n", __func__, message);

    ok = (message != NULL);

    free(message);

    return ok;
}

bool
test_explicit_response_code(void)
{
    globus_object_t                    *err = NULL;
    int                                 code = 0;
    bool                                ok = false;

    err = GlobusGFSErrorObjFtpResponse(NULL, 550, "NOT_FOUND", NULL);

    code = globus_gfs_error_get_ftp_response_code(err);

    ok = (code == 550);
    globus_object_free(err);

    return ok;
}

bool
test_explicit_message(void)
{
    globus_object_t                    *err = NULL;
    const char                         *expect = "SOME_EXTENSION_ERROR";
    char                               *message = NULL;
    char                               *m = NULL;
    bool                                ok = false;

    err = GlobusGFSErrorObjFtpResponse(NULL, 550, expect, NULL);

    message = globus_object_printable_to_string(err);
    fprintf(stderr, "# %s \"%s\"\n", __func__, message);

    m = strstr(message, " c=");
    if (m != NULL)
    {
        m += 3;
    }

    ok = (m != NULL) && (strncmp(m, expect, strlen(expect)) == 0);
    globus_object_free(err);
    free(message);

    return ok;
}

static
bool
test_error_macros(void)
{
    globus_result_t                     result = GLOBUS_SUCCESS;
    bool                                ok = false;

    result = GlobusGFSErrorMemory("memory");
    if (result == GLOBUS_SUCCESS)
    {
        goto fail;
    }

    result = GlobusGFSErrorPathNotFound("path");
    if (result == GLOBUS_SUCCESS)
    {
        goto fail;
    }
    result = GlobusGFSErrorIncorrectChecksum("1", "2");
    if (result == GLOBUS_SUCCESS)
    {
        goto fail;
    }

    result = GlobusGFSErrorMultipartUploadNotFound();
    if (result == GLOBUS_SUCCESS)
    {
        goto fail;
    }

    result = GlobusGFSErrorAmbiguousPath("ambiguity");
    if (result == GLOBUS_SUCCESS)
    {
        goto fail;
    }

    result = GlobusGFSErrorTooBusy();
    if (result == GLOBUS_SUCCESS)
    {
        goto fail;
    }
    result = GlobusGFSErrorDataChannelAuthenticationFailure();
    if (result == GLOBUS_SUCCESS)
    {
        goto fail;
    }
    result = GlobusGFSErrorDataChannelCommunicationFailure();
    if (result == GLOBUS_SUCCESS)
    {
        goto fail;
    }
    result = GlobusGFSErrorLoginDenied();
    if (result == GLOBUS_SUCCESS)
    {
        goto fail;
    }
    result = GlobusGFSErrorPermissionDenied();
    if (result == GLOBUS_SUCCESS)
    {
        goto fail;
    }
    result = GlobusGFSErrorQuotaExceeded();
    if (result == GLOBUS_SUCCESS)
    {
        goto fail;
    }

    result = GlobusGFSErrorNoSpaceLeft();
    if (result == GLOBUS_SUCCESS)
    {
        goto fail;
    }

    result = GlobusGFSErrorInvalidPathName("invalid");
    if (result == GLOBUS_SUCCESS)
    {
        goto fail;
    }

    result = GlobusGFSErrorPathExists("path");
    if (result == GLOBUS_SUCCESS)
    {
        goto fail;
    }

    result = GlobusGFSErrorIsADirectory("name");
    if (result == GLOBUS_SUCCESS)
    {
        goto fail;
    }

    result = GlobusGFSErrorNotADirectory("name");
    if (result == GLOBUS_SUCCESS)
    {
        goto fail;
    }

    result = GlobusGFSErrorCRLError();
    if (result == GLOBUS_SUCCESS)
    {
        goto fail;
    }

    result = GlobusGFSErrorInternalError(__func__);
    if (result == GLOBUS_SUCCESS)
    {
        goto fail;
    }

    result = GlobusGFSErrorNotImplemented();
    if (result == GLOBUS_SUCCESS)
    {
        goto fail;
    }

    result = GlobusGFSErrorConfigurationError();
    if (result == GLOBUS_SUCCESS)
    {
        goto fail;
    }
    ok = true;
fail:

    return ok;
}

static
bool
test_error_system(void)
{
    globus_object_t                    *err = NULL;
    int                                 code = 0;
    bool                                ok = false;

    errno = ENOENT;
    err = globus_i_gfs_error_system(0, 0, "Unknown error");

    code = globus_gfs_error_get_ftp_response_code(err);

    fprintf(stderr, "# %s %d\n", __func__, code);

    ok = (code == 451);
    globus_object_free(err);

    return ok;
}

static
bool
test_error_multiline(void)
{
    globus_object_t                    *err = NULL;
    char                               *msg = NULL;
    char                               *ftp_str = NULL;
    bool                                ok = false;
    char                               *p = NULL;
    int                                 count=0;

            
    err = GlobusGFSErrorObjAmbiguousPath(NULL, "/Some/Dupe/Path");

    msg = globus_error_print_friendly(err);
    ftp_str = globus_gsc_string_to_959(
            globus_gfs_error_get_ftp_response_code(err), msg, NULL);

    p = ftp_str;

    while (*p != 0)
    {
        char *q = strstr(p, "\r\n");

        if (q)
        {
            *q = 0;
            fprintf(stderr, "# %s: %s\n", __func__, p);
            count++;
            p = q+2;
        }
        else
        {
            fprintf(stderr, "# %s: %s\n", __func__, p);
            count++;
            p += strlen(p);
        }
    }
    ok = (count == 3);
    
    free(msg);

    globus_object_free(err);

    return ok;
}

static
bool
test_error_response_error_code(void)
{
    globus_object_t                    *err = NULL;
    const char *                        error_code = NULL;
    bool                                ok = false;

    err = GlobusGFSErrorObjFtpResponse(NULL, 550, "NOT_FOUND", NULL);

    error_code = globus_gfs_error_get_ftp_response_error_code(err);

    ok = (strcmp(error_code, "NOT_FOUND") == 0);
    globus_object_free(err);

    return ok;
}
        
static
bool
test_error_response_error_code_match(void)
{
    globus_object_t                    *err = NULL;
    bool                                ok = false;

    err = GlobusGFSErrorObjFtpResponse(NULL, 550, "NOT_FOUND", NULL);

    ok = globus_gfs_error_match_response_error_code(err, "NOT_FOUND");

    globus_object_free(err);

    return ok;
}

static
bool
test_error_response_error_code_match_null(void)
{
    globus_object_t                    *err = NULL;
    bool                                ok = false;

    err = GlobusGFSErrorObjFtpResponse(NULL, 550, "NOT_FOUND", NULL);

    ok = !globus_gfs_error_match_response_error_code(err, NULL);

    globus_object_free(err);

    return ok;
}

static
bool
test_error_response_error_code_match_null_err(void)
{
    globus_object_t                    *err = NULL;
    bool                                ok = false;

    ok = !globus_gfs_error_match_response_error_code(NULL, "NOT_FOUND");

    globus_object_free(err);

    return ok;
}

static
bool
test_error_response_error_code_mismatch(void)
{
    globus_object_t                    *err = NULL;
    bool                                ok = false;

    err = GlobusGFSErrorObjFtpResponse(NULL, 550, "NOT_FOUND", NULL);

    ok = !globus_gfs_error_match_response_error_code(err, "CHEESE");

    globus_object_free(err);

    return ok;
}

typedef struct
{
    bool                              (*test_case)(void);
    const char                         *name;
}
test_case_t;

#define TEST(x) { x, #x }

int main()
{
    test_case_t                         tests[] =
    {
        TEST(test_default_message),
        TEST(test_explicit_response_code),
        TEST(test_explicit_message),
        TEST(test_error_macros),
        TEST(test_error_system),
        TEST(test_error_multiline),
        TEST(test_error_response_error_code),
        TEST(test_error_response_error_code_match),
        TEST(test_error_response_error_code_match_null),
        TEST(test_error_response_error_code_match_null_err),
        TEST(test_error_response_error_code_mismatch),
    };
    size_t                              num_tests
                                      = sizeof(tests)/sizeof(tests[0]);
    int                                 failed = 0;

    globus_module_activate(GLOBUS_GRIDFTP_SERVER_MODULE);

    printf("1..%zu\n", num_tests);
    for (size_t i = 0; i < num_tests; i++)
    {
        bool                            ok = false;

        ok = tests[i].test_case();

        if (ok)
        {
            printf("ok %zu - %s\n", i+1, tests[i].name);
        }
        else
        {
            printf("not ok %zu - %s\n", i+1, tests[i].name);
            failed++;
        }
    }
    globus_module_deactivate_all();
    return failed;
}
