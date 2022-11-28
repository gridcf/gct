#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#include "globus_preload.h"
    
typedef struct
{
    char *alias;
    size_t alias_len;
} globus_l_gfs_alias_ent_t;

extern int
globus_list_cmp_alias_ent(
    void *                              a, 
    void *                              b,
    void *                              arg);

typedef struct
{
    int idx1;
    int idx2;
    int expect;
} test_t;
int main()
{
    globus_l_gfs_alias_ent_t ents[] = 
    {
        { .alias = "hell*", .alias_len = 5 },
        { .alias = "hello", .alias_len = 5 },
        { .alias = "*", .alias_len = 1 },
        { .alias = "", .alias_len = 0 },
        { .alias = "hell", .alias_len = 4 },
        { .alias = (char[]){(char)-1, (char)0 }, .alias_len = 1 },
        { .alias = "hell?", .alias_len = 5 },
        { .alias = "hell[o]", .alias_len = 7 }
    };
    test_t tests[] = 
    {
        {0, 0, 1},
        {0, 1, 0},
        {0, 2, 1},
        {0, 3, 1},
        {0, 4, 1},
        {0, 5, 0},
        {0, 6, 0},
        {0, 7, 0},
        {1, 0, 1},
        {1, 1, 1},
        {1, 2, 1},
        {1, 3, 1},
        {1, 4, 1},
        {1, 5, 0},
        {1, 6, 1},
        {1, 7, 1},
        {2, 0, 0},
        {2, 1, 0},
        {2, 2, 1},
        {2, 3, 1},
        {2, 4, 0},
        {2, 5, 0},
        {2, 6, 0},
        {2, 7, 0},
        {3, 0, 0},
        {3, 1, 0},
        {3, 2, 0},
        {3, 3, 1},
        {3, 4, 0},
        {3, 5, 0},
        {3, 6, 0},
        {3, 7, 0},
        {4, 0, 0},
        {4, 1, 0},
        {4, 2, 1},
        {4, 3, 1},
        {4, 4, 1},
        {4, 5, 0},
        {4, 6, 0},
        {4, 7, 0},
        {5, 0, 1},
        {5, 1, 1},
        {5, 2, 1},
        {5, 3, 1},
        {5, 4, 1},
        {5, 5, 1},
        {5, 6, 1},
        {5, 7, 1},
        {6, 0, 1},
        {6, 1, 0},
        {6, 2, 1},
        {6, 3, 1},
        {6, 4, 1},
        {6, 5, 0},
        {6, 6, 1},
        {6, 7, 0},
        {7, 0, 1},
        {7, 1, 0},
        {7, 2, 1},
        {7, 3, 1},
        {7, 4, 1},
        {7, 5, 0},
        {7, 6, 1},
        {7, 7, 1},
    };
    int test_count = (int) (sizeof(tests)/sizeof(tests[0]));
    int failed_tests = 0;

    LTDL_SET_PRELOADED_SYMBOLS();

    printf("1..%d\n", test_count);

    for (int i = 0; i < test_count; i++)
    {
        int rc = globus_list_cmp_alias_ent(&ents[tests[i].idx1], &ents[tests[i].idx2], NULL);
        char *s;
        size_t slen;
        if (rc == tests[i].expect)
        {
            printf("ok %d", i+1);
        }
        else
        {
            printf("not ok %d", i+1);
            failed_tests++;
        }
        printf(" - \"");
        s = ents[tests[i].idx1].alias;
        slen = strlen(s);
        for (int j = 0; j < slen; j++)
        {
            if (isprint(s[j]))
            {
                putchar(s[j]);
            }
            else
            {
                printf("%#0x", (int) (unsigned char) s[j]);
            }
        }
        printf("\" %s \"", tests[i].expect ? ">=" : "<");
        s = ents[tests[i].idx2].alias;
        slen = strlen(s);
        for (int j = 0; j < slen; j++)
        {
            if (isprint(s[j]))
            {
                putchar(s[j]);
            }
            else
            {
                printf("%#0x", (int) (unsigned char) s[j]);
            }
        }
        printf("\"\n");
    }

    return failed_tests;
}
