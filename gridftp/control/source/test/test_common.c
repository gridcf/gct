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

#include <stdarg.h>
#include "globus_ftp_control_test.h"
#include "globus_common.h"
#include <string.h>
#include "test_common.h"

int                                      verbose_print_level;

void
ftp_test_monitor_reset(
    ftp_test_monitor_t *                     monitor)
{
    monitor->count = 0;
    monitor->done = GLOBUS_FALSE;
    monitor->result = GLOBUS_SUCCESS;
}

void
ftp_test_monitor_init(
    ftp_test_monitor_t *                     monitor)
{
    globus_mutex_init(&monitor->mutex, GLOBUS_NULL);
    globus_cond_init(&monitor->cond, GLOBUS_NULL);

    ftp_test_monitor_reset(monitor);
}

void
ftp_test_monitor_done_wait(
    ftp_test_monitor_t *                     monitor)
{
    globus_mutex_lock(&monitor->mutex);
    {
        while(!monitor->done)
        {
            globus_cond_wait(&monitor->cond, &monitor->mutex);
        }
    }
    globus_mutex_unlock(&monitor->mutex);
}

void
ftp_test_monitor_count_wait(
    ftp_test_monitor_t *                     monitor,
    int                                      count)
{
    globus_mutex_lock(&monitor->mutex);
    {
        while(monitor->count < count)
        {
            globus_cond_wait(&monitor->cond, &monitor->mutex);
        }
    }
    globus_mutex_unlock(&monitor->mutex);
}

void
ftp_test_monitor_signal(
    ftp_test_monitor_t *                     monitor)
{
    globus_mutex_lock(&monitor->mutex);
    {
        monitor->done = GLOBUS_TRUE;
        monitor->count++;
        globus_cond_signal(&monitor->cond);
    }
    globus_mutex_unlock(&monitor->mutex);
}

void
ftp_test_monitor_destroy(
    ftp_test_monitor_t *                     monitor)
{
    globus_mutex_destroy(&monitor->mutex);
    globus_cond_destroy(&monitor->cond);
}

void
help_print()
{
    printf("globus_ftp_control_test arguments:\n");
    printf("--verbose [level]\n");
    printf("    level 0 -- print overall success [default level]\n");
    printf("    level 1 -- print individual test success\n");
    printf("    level 2 -- print intermediate test messages\n");
    printf("    level 3 -- print in loop test messages\n");
    printf("--help\n");
}

void
fake_file_init(
    globus_ftp_control_fake_file_t *          fake_file,
    int                                       file_size,
    int                                       chunk_size)
{ 
    int                                       offset;
    int                                       len;
    char *                                    buf = 
        "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ1234567890";

    fake_file->chunk_size = chunk_size;
    fake_file->buffer = globus_malloc(chunk_size);
    fake_file->file_size = file_size;
    fake_file->offset = 0;

    offset = 0;
    while(offset < chunk_size)
    {
        len = chunk_size - offset;
        if(len > strlen(buf))
        {
            len = strlen(buf);
        }
        memcpy(&fake_file->buffer[offset], buf, len);
        offset += len;
    }
}

void 
fake_file_destroy(
    globus_ftp_control_fake_file_t *          fake_file)
{
    globus_free(fake_file->buffer);
}

globus_byte_t *
fake_file_get_chunk(
    globus_ftp_control_fake_file_t *          fake_file,
    int *                                     size)
{
    int                                       offset;

    offset = fake_file->offset % fake_file->chunk_size;

    *size = fake_file->chunk_size - offset;

    if(*size + fake_file->offset > fake_file->file_size)
    {
        *size = fake_file->file_size - fake_file->offset;
    }
    fake_file->offset += *size;

    return fake_file->buffer;
}

globus_bool_t
fake_file_is_eof(
    globus_ftp_control_fake_file_t *          fake_file)
{
    if(fake_file->offset >= fake_file->file_size)
    {
        return GLOBUS_TRUE;
    }
    else
    {
        return GLOBUS_FALSE;
    }
}

globus_bool_t
fake_file_seek(
    globus_ftp_control_fake_file_t *          fake_file,
    int                                       offset)
{
    if(fake_file->file_size > offset)
    {
        return GLOBUS_FALSE;
    }

    fake_file->offset = offset;

    return GLOBUS_TRUE;
}

globus_bool_t
fake_file_cmp(
    globus_ftp_control_fake_file_t *          fake_file,
    globus_byte_t *                           buffer,
    int                                       offset,
    int                                       length)
{
    int                                       start;
    int                                       ndx = 0;
    int                                       len = 0;

    start = offset % fake_file->chunk_size;
    while(ndx < length)
    {
        if(length + start - ndx < fake_file->chunk_size)
        {
           len = length - ndx;
        }
        else
        {
            len = fake_file->chunk_size - ndx - start;
        }
        
        if(memcmp(&buffer[ndx], &fake_file->buffer[start], len) != 0)
        {
            return GLOBUS_FALSE;
        }
        start = 0;
        ndx += len;
    }

    return GLOBUS_TRUE;
}

void 
verbose_printf(
    int                                       level, 
    char *                                    s, 
    ...)
{
    va_list                                   ap;

    if(level > verbose_print_level)
    {
       return;
    }
    va_start(ap, s);
    vprintf(s, ap);
    va_end(ap);
    fflush(stdout);
}

globus_bool_t
pasv_to_host_port(
    char *                                      astr,
    globus_ftp_control_host_port_t *            addr)
{
    char *                                      hostname;
    char *                                      port_str;
    char *                                      tmp_ptr;
    unsigned short                              port;
    int                                         hi;
    int                                         low;
    int                                         ctr;

    hostname = strchr(astr, '(') + 1;

    tmp_ptr = strchr(hostname, ',');
    for(ctr = 0; ctr < 3; ctr++)
    {
        if(tmp_ptr == GLOBUS_NULL)
        {
            return GLOBUS_FALSE;
        }
        tmp_ptr[0] = '.';
        tmp_ptr++;
        tmp_ptr = strchr(tmp_ptr, ',');
    }
    tmp_ptr[0] = '\0';

    port_str = tmp_ptr + 1;

    sscanf(port_str, "%d,%d", &hi, &low);
    port = hi * 256;
    port = port | low;

    globus_ftp_control_host_port_init(
        addr,
        hostname,
        port);

    return GLOBUS_TRUE;
}
