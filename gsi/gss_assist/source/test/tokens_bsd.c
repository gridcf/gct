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

#ifndef GLOBUS_DONT_DOCUMENT_INTERNAL
/**
 * @file tokens_bsd.c
 * @author Sam Lang, Sam Meder
 */
#endif

#include "globus_common.h"
#include "globus_i_gss_assist.h"

int
token_bsd_get(
    void *                              arg,
    void **                             bufp,
    size_t *                            sizep)
{
    unsigned char                       int_buf[5];
    unsigned char *                     pp;
    unsigned char *                     bp = NULL;
    int                                 bsize;
    int                                 dsize;
    int                                 size;
    void *                              cp;
    int                                 sock;
    int                                 bytesread;
    int                                 return_value = 0;
    static char *                       _function_name_ =
        "token_bsd_get";
    GLOBUS_I_GSI_GSS_ASSIST_DEBUG_ENTER;

    sock = (intptr_t) arg;
    if ((bytesread = recv(sock, int_buf, 4, 0)) != 4)
    {
        fprintf(stderr,_GASL("Failed reading length %d\n"),bytesread);
        return_value = GLOBUS_GSS_ASSIST_TOKEN_EOF;
        goto exit;
    }

    GLOBUS_I_GSI_GSS_ASSIST_DEBUG_FPRINTF(
        4, (globus_i_gsi_gss_assist_debug_fstream,
            _GASL("token read:%2.2x%2.2x%2.2x%2.2x\n"),
            int_buf[0], int_buf[1], int_buf[2], int_buf[3]));

    /*
     * check if the length is missing, and we are receiving
     * a SSL token directly.
     * SSLv3 will start with a flag byte in the twenties
     * followed by major version 3 minor version 0
     * Will also accept a SSLv2 hello 2 0
     * or a TLS  3 1
     */

    if (((int_buf[0] >= 20) && (int_buf[0] <= 26)
         && ((int_buf[1] == 3)
             || (int_buf[1] == 2 && int_buf[2] == 0)))
        || ((int_buf[0] & 0x80) && int_buf[2] == 1))
    {
        /* looks like a SSL token read rest of length */

        if (recv(sock, &int_buf[4], 1, 0) != 1)
        {
            GLOBUS_I_GSI_GSS_ASSIST_DEBUG_FPRINTF(
                3, (globus_i_gsi_gss_assist_debug_fstream,
                    "%s", _GASL("FAILED READING EXTRA BYTE\n")));
            return_value = GLOBUS_GSS_ASSIST_TOKEN_EOF;
            goto exit;
        }

        GLOBUS_I_GSI_GSS_ASSIST_DEBUG_FPRINTF(
            4, (globus_i_gsi_gss_assist_debug_fstream,
                _GASL("reading SSL token %.2x%.2x%.2x%.2x%.2x\n"),
                int_buf[0], int_buf[1], int_buf[2], int_buf[3], int_buf[4]));

        if ((int_buf[0] & 0x80)) {
            /* looks like a sslv2 hello
             * length is of following bytes in header.
             * we read in 5, 2 length and 3 extra,
             * so only need next dsize - 3
             */
            dsize = ( (((unsigned int) (int_buf[0] & 0x7f)) << 8)
                    |  ((unsigned int) int_buf[1]) ) - 3;
        } else {
            dsize = ( (((unsigned int) int_buf[3]) << 8)
                    |  ((unsigned int) int_buf[4]) );
        }

        /* If we are using the globus_ssleay, with
         * international version, we may be using the
         * "26" type, where the length is really the hash
         * length, and there is a hash, 8 byte seq andi
         * 4 byte data length following. We need to get
         * these as well.
         */

        if (int_buf[0] == 26)
        {
            bsize = dsize + 12;  /* MD, seq, data-length */
            bp = (void *)malloc(bsize);
            if (!bp)
            {
                return_value = GLOBUS_GSS_ASSIST_TOKEN_ERR_MALLOC;
                goto exit;
            }
            if (recv(sock, bp, bsize, 0) != bsize)
            {
                return_value = GLOBUS_GSS_ASSIST_TOKEN_EOF;
                goto exit;
            }
            dsize = ( (((unsigned int) bp[bsize-4]) << 24)
                    | (((unsigned int) bp[bsize-3]) << 16)
                    | (((unsigned int) bp[bsize-2]) << 8)
                    |  ((unsigned int) bp[bsize-1]) );

            size = bsize + dsize + 5;
        }
        else
        {
            size = dsize + 5;
        }
        cp = (void *)malloc(size);
        if (!cp)
        {
            return_value = GLOBUS_GSS_ASSIST_TOKEN_ERR_MALLOC;
            goto exit;
        }

        /* reassemble token header from in_buf and bp */

        pp = cp;
        memcpy(pp, int_buf, 5);
        pp += 5;
        if (bp)
        {
            memcpy(pp, bp, bsize);
            pp += bsize;
            free(bp);
            bp = NULL;
        }
        if ((bytesread = recv(sock, pp, dsize, 0)) != dsize)
        {
            GLOBUS_I_GSI_GSS_ASSIST_DEBUG_FPRINTF(
                3, (globus_i_gsi_gss_assist_debug_fstream,
                    _GASL("READ SHORT: %d, %d\n"), dsize, bytesread));
            return_value = GLOBUS_GSS_ASSIST_TOKEN_EOF;
            goto exit;
        }
    }
    else
    {
        size = ( (((unsigned int) int_buf[0]) << 24)
               | (((unsigned int) int_buf[1]) << 16)
               | (((unsigned int) int_buf[2]) << 8)
               |  ((unsigned int) int_buf[3]) );

        if (size > (1 << 24) || size < 0)  /* size may be garbage */
        {
            return_value = GLOBUS_GSS_ASSIST_TOKEN_ERR_BAD_SIZE;
            goto exit;
        }

        cp = (void *)malloc(size);
        if (!cp)
        {
            return_value = GLOBUS_GSS_ASSIST_TOKEN_ERR_MALLOC;
        }
        if ((bytesread = recv(sock, cp, size, 0)) != size)
        {
            GLOBUS_I_GSI_GSS_ASSIST_DEBUG_FPRINTF(
                3, (globus_i_gsi_gss_assist_debug_fstream,
                    _GASL("read short: %d, %d\n"), size, bytesread));
            return_value = GLOBUS_GSS_ASSIST_TOKEN_EOF;
            goto exit;
        }
    }

    *bufp = cp;
    *sizep = size;

 exit:

    GLOBUS_I_GSI_GSS_ASSIST_DEBUG_EXIT;
    return return_value;
}
/* token_bsd_get() */

int
token_bsd_send(
    void *                              arg,
    void *                              buf,
    size_t                              size)
{
    int                                 return_value = 0;
    globus_gss_assist_ex                ex;
    static char *                       _function_name_ =
        "token_bsd_send";
    GLOBUS_I_GSI_GSS_ASSIST_DEBUG_ENTER;

    ex.arg = arg;
    ex.flags = 0;

    return_value = token_bsd_send_ex((void *)&ex, buf, size);

    GLOBUS_I_GSI_GSS_ASSIST_DEBUG_EXIT;
    return return_value;
}
/* globus_gss_assist_token_send_fd */


int
token_bsd_send_ex(
    void *                              exp,
    void *                              buf,
    size_t                              size)
{
    int                                 return_value = 0;
    unsigned char                       int_buf[4];
    char *                              header = (char *)buf;
    int                                 sock;
    globus_gss_assist_ex *              ex;

    static char *                       _function_name_ =
        "globus_gss_assist_token_send_fd_ex";
    GLOBUS_I_GSI_GSS_ASSIST_DEBUG_ENTER;

    ex = (globus_gss_assist_ex *) exp;
    sock = (intptr_t) ex->arg;

    /*
     * Will always send SSL type tokens without a length
     */

    GLOBUS_I_GSI_GSS_ASSIST_DEBUG_FPRINTF(
        3, (globus_i_gsi_gss_assist_debug_fstream,
            _GASL("send_token: flags: %d length: %u\n"),
            ex->flags, size));

    if (!(size > 5 && header[0] <= 26 && header[0] >= 20
          && ((header[1] == 3)
              || (header[1] == 2 && header[2] == 0))))
    {

        if (!(ex->flags & GLOBUS_GSS_ASSIST_EX_SEND_WITHOUT_LENGTH))
        {
            int_buf[0] = size >> 24;
            int_buf[1] = size >> 16;
            int_buf[2] = size >>  8;
            int_buf[3] = size;

            GLOBUS_I_GSI_GSS_ASSIST_DEBUG_FPRINTF(
                3, (globus_i_gsi_gss_assist_debug_fstream,
                    "%s", _GASL("with 4 byte length")));

            if (send(sock, int_buf, 4, 0) != 4)
            {
                return_value = GLOBUS_GSS_ASSIST_TOKEN_EOF;
                goto exit;
            }
        }
    }

    GLOBUS_I_GSI_GSS_ASSIST_DEBUG_PRINT(3, "\n");

    if (send(sock, buf, size, 0) != size)
    {
        return_value = GLOBUS_GSS_ASSIST_TOKEN_EOF;
        goto exit;
    }

 exit:
    GLOBUS_I_GSI_GSS_ASSIST_DEBUG_EXIT;
    return return_value;
}
/* token_bsd_send_ex() */
