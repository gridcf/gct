#include <stdarg.h>
#include <stdio.h>

/**
 * @file
 * @brief Test Anything Protocol implementation
 */

static int total = 0;
static int failed = 0;
static int skipped = 0;
static void ok(int predval, const char *fmt, ...)
{
    static int testno=1;
    va_list ap;
    printf("%s %d - ", predval ? "ok" : "not ok", testno++);
    va_start(ap, fmt);
    vprintf(fmt, ap);
    va_end(ap);
    printf("\n");
    if (!predval)
    {
        failed++;
    }
    total++;
}

#if __STDC_VERSION__ >= 199901L
#define get_explanationok(predval, ...) __VA_ARGS__

/* This only works if the second parameter is a call to the ok function */
#define skip(skip_predicate, ...) \
    if (skip_predicate) \
    { \
        ok(1, " # SKIP (" #skip_predicate  ") " get_explanation ## __VA_ARGS__); \
        skipped++; \
    } \
    else \
    { \
        __VA_ARGS__; \
    }
#else
#define skip(skip_predicate, okcall) \
    if (skip_predicate) \
    { \
        ok(1, " # SKIP (" #skip_predicate  ") "); \
        skipped++; \
    } \
    else \
    { \
        okcall; \
    }
#endif

#define TEST_EXIT_CODE (skipped == total) ? 77 : failed
#define TEST_ASSERT(assertion) if (!(assertion)) { fprintf(stderr, "%s:%d:%s %s\n", __FILE__, __LINE__, __func__, #assertion); return 1; }
#define TEST_ASSERT_RESULT_SUCCESS(result) \
    if (result) { \
        char * err = globus_error_print_friendly(globus_error_peek(result)); \
        fprintf(stderr, "%s:%d:%s %s\n", __FILE__, __LINE__, __func__, err); \
        free(err); \
        return 1; \
    }
