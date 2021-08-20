
#ifndef __VOMS_UTILS_H_
#define __VOMS_UTILS_H_

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <assert.h>

#include <sys/stat.h>
#include <dirent.h>

#include <openssl/x509.h>
#include <openssl/objects.h>
#include <openssl/asn1.h>
#include <openssl/pem.h>

/*
 * get_vomses()
 *
 * Returns the vomses line for specified path.
 * Returns the pointer to vomses line if succeeded, NULL otherwise.
 */
char **get_vomses(const char *path);

/*
 * has_voms_extension()
 *
 * Returns 1 if specified file has VOMS extension.
 * Returns 0 if specified file has no VOMS extension.
 * Returns -1 if error occurred.
 */
int has_voms_extension(const char *certfilepath);


#endif

