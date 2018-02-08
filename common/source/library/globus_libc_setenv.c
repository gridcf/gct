#include "globus_config.h"
#include <stdio.h>
#include <stdlib.h>

#ifdef globus_libc_setenv
#undef globus_libc_setenv
#endif
int
globus_libc_setenv(
    register const char *name,
    register const char *value,
    int rewrite)
{
#ifdef _WIN32
    size_t len = snprintf(NULL, 0, "%s=%s", name, value) + 1;
    char *tmp;
    if (rewrite || !getenv(name))
    {
        tmp = malloc(len);
        sprintf(tmp, "%s=%s", name, value);
        return _putenv(tmp);
    }
    return 0;
#else
    return setenv(name, value, rewrite);
#endif
}

#ifdef  globus_libc_unsetenv
#undef globus_libc_unsetenv
#endif
void
globus_libc_unsetenv(
    const char *name)
{
#ifdef _WIN32
    size_t len = snprintf(NULL, 0, "%s=", name) + 1;
    char *tmp = malloc(len);
    sprintf(tmp, "%s=", name);
    _putenv(tmp);
#else
    unsetenv(name);
#endif
}

#ifdef globus_libc_getenv
#undef globus_libc_getenv
#endif
char *
globus_libc_getenv(const char *name)
{
    return getenv(name);
}
