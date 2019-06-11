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

#include "globus_common.h"
#include "globus_gridftp_server.h"
#include "globus_xio.h"
#include "globus_xio_file_driver.h"
#include <openssl/md5.h>
#include <openssl/sha.h>
#include <zlib.h>
#include "version.h"

#include <utime.h>
#ifndef TARGET_ARCH_WIN32
#include <grp.h>
#endif

#ifdef TARGET_ARCH_WIN32
#include <time.h>
#define S_ISLNK(x) 0
#define lstat(x,y) stat(x,y)
#define mkdir(x,y) mkdir(x)
#define chown(x,y,z) -1
#define symlink(x,y) -1
#define readlink(x,y,z) 0
#define realpath(x,y) strcpy(y,x)
#define scandir(a,b,c,d) 0
#define alphasort(x,y) 0
#define getgrnam(x) 0
#endif


#ifndef MAXPATHLEN
#define MAXPATHLEN 4096
#endif

#define GFS_L_FILE_CKSM_SUPPORT "MD5:10;ADLER32:10;SHA1:10;SHA256:11;SHA512:12;"

GlobusDebugDeclare(GLOBUS_GRIDFTP_SERVER_FILE);

#define GlobusGFSFileDebugPrintf(level, message)                             \
    GlobusDebugPrintf(GLOBUS_GRIDFTP_SERVER_FILE, level, message)

#define GlobusGFSFileDebugEnter()                                            \
    GlobusGFSFileDebugPrintf(                                                \
        GLOBUS_GFS_DEBUG_TRACE,                                              \
        ("[%s] Entering\n", _gfs_name))
        
#define GlobusGFSFileDebugExit()                                             \
    GlobusGFSFileDebugPrintf(                                                \
        GLOBUS_GFS_DEBUG_TRACE,                                              \
        ("[%s] Exiting\n", _gfs_name))

#define GlobusGFSFileDebugExitWithError()                                    \
    GlobusGFSFileDebugPrintf(                                                \
        GLOBUS_GFS_DEBUG_TRACE,                                              \
        ("[%s] Exiting with error\n", _gfs_name))

GlobusDebugDefine(GLOBUS_GRIDFTP_SERVER_FILE);

typedef void
(*globus_l_gfs_file_cksm_cb_t)(
    globus_result_t                     result,
    char *                              cksm,
    void *                              user_arg);


enum
{
    GLOBUS_GFS_FILE_CKSM_TYPE_NONE = 0,
    GLOBUS_GFS_FILE_CKSM_TYPE_ADLER32,
    GLOBUS_GFS_FILE_CKSM_TYPE_MD5,
    GLOBUS_GFS_FILE_CKSM_TYPE_SHA1,
    GLOBUS_GFS_FILE_CKSM_TYPE_SHA256,
    GLOBUS_GFS_FILE_CKSM_TYPE_SHA512
};

typedef struct globus_l_gfs_file_cksm_monitor_s
{
    globus_gfs_operation_t              op;
    globus_off_t                        offset;
    globus_off_t                        length;
    globus_off_t                        count;
    globus_off_t                        read_left;
    globus_size_t                       block_size;
    globus_l_gfs_file_cksm_cb_t         internal_cb;
    void *                              internal_cb_arg;
    
    globus_callback_handle_t            marker_handle;
    int                                 marker_freq;
    globus_bool_t                       send_marker;
    globus_off_t                        total_bytes;

    int                                 delay_read;

    unsigned char                       cksum_type;
    MD5_CTX                             mdctx;
    SHA_CTX                             sha1ctx;
    SHA256_CTX                          sha256ctx;
    SHA512_CTX                          sha512ctx;
    uint32_t                            adler32ctx;

    globus_byte_t                       buffer[];
} globus_l_gfs_file_cksm_monitor_t;

typedef struct 
{
    gss_cred_id_t                       cred;
    char *                              sbj;
    char *                              username;
    char *                              pw;
} gfs_l_file_session_t;

typedef struct
{
    globus_mutex_t                      lock;
    globus_memory_t                     mem;
    globus_priority_q_t                 queue;
    globus_list_t *                     buffer_list;
    globus_gfs_operation_t              op;
    char *                              pathname;
    globus_xio_handle_t                 file_handle;
    globus_off_t                        file_offset;
    globus_off_t                        read_offset;
    globus_off_t                        read_length;
    int                                 pending_writes;
    int                                 pending_reads;
    globus_size_t                       block_size;
    int                                 optimal_count;
    int                                 node_ndx;
    globus_object_t *                   error;
    globus_bool_t                       first_read;
    globus_bool_t                       eof;
    globus_bool_t                       aborted;
    int                                 concurrency_check;
    int                                 concurrency_check_interval;
    char *                              expected_cksm;
    char *                              expected_cksm_alg;
    time_t                              utime;
    /* added for multicast stuff, but cold be generally useful */
    gfs_l_file_session_t *              session;

    globus_result_t                     finish_result;
} globus_l_file_monitor_t;


typedef struct
{
    globus_byte_t *                     buffer;
    globus_off_t                        offset;
    globus_size_t                       length;
} globus_l_buffer_info_t;

typedef struct gfs_l_file_stack_entry_s
{
    globus_xio_driver_t                 driver;
    char *                              driver_name;
    char *                              opts;
} gfs_l_file_stack_entry_t;

static globus_xio_driver_t              globus_l_gfs_file_driver;

static
globus_result_t
globus_l_gfs_file_utime(
    globus_gfs_operation_t              op,
    const char *                        pathname,
    time_t                              modtime);

static
globus_result_t
globus_l_gfs_file_cksm(
    globus_gfs_operation_t              op,
    const char *                        pathname,
    const char *                        algorithm,
    globus_off_t                        offset,
    globus_off_t                        length,
    globus_l_gfs_file_cksm_cb_t         internal_cb,
    void *                              internal_cb_arg);
    
static
globus_result_t
globus_l_gfs_file_make_stack(
    globus_gfs_operation_t              op,
    globus_l_file_monitor_t *           mon,
    globus_xio_attr_t                   attr,
    globus_xio_stack_t                  stack,
    gfs_l_file_session_t *              session_h)
{
    globus_xio_driver_t                 driver;
    globus_result_t                     result;
    globus_list_t *                     driver_list = NULL;
    globus_xio_driver_list_ent_t *      ent;
    GlobusGFSName(globus_l_gfs_file_make_stack);

    globus_gfs_data_get_file_stack_list(op, &driver_list);

    /* set the cred in case anyone wants it */
    globus_xio_attr_cntl(
        attr,
        NULL,
        GLOBUS_XIO_ATTR_SET_CREDENTIAL,
        session_h->cred,
        session_h->sbj,
        session_h->username,
        session_h->pw);

    if(driver_list == NULL)
    {
        result = globus_xio_stack_push_driver(
            stack, globus_l_gfs_file_driver);
        if(result != GLOBUS_SUCCESS)
        {
            result = GlobusGFSErrorWrapFailed(
                "globus_xio_stack_push_driver", result);
            goto error_push;
        }
    }
    else
    {
        while(!globus_list_empty(driver_list))
        {
            ent = (globus_xio_driver_list_ent_t *)
                globus_list_remove(&driver_list, driver_list);

            if(strcmp(ent->driver_name, "file") == 0)
            {
                driver = globus_l_gfs_file_driver;
                result = globus_xio_stack_push_driver(
                    stack, globus_l_gfs_file_driver);
            }
            else
            {
                driver = ent->driver;
                result = globus_xio_stack_push_driver(stack, ent->driver);
            }
            if(result != GLOBUS_SUCCESS)
            {
                result = GlobusGFSErrorWrapFailed(
                    "globus_xio_stack_push_driver", result);
                goto error_push;
            }

            if(ent->opts != NULL)
            {
                /* ignore error */
                globus_xio_attr_cntl(
                    attr,
                    driver,
                    GLOBUS_XIO_SET_STRING_OPTIONS,
                    ent->opts);
            }
        }
    }

    return GLOBUS_SUCCESS;

error_push:
    return result;
}



/*
 * if priority_1 comes after priority_2, return > 0
 * else if priority_1 comes before priority_2, return < 0
 * else return 0
 */
 
static
int
globus_l_gfs_file_queue_compare(
    void *                              priority_1,
    void *                              priority_2)
{
    globus_l_buffer_info_t *            buf_info1;
    globus_l_buffer_info_t *            buf_info2;
    int                                 rc = 0;
    GlobusGFSName(globus_l_gfs_file_queue_compare);
    GlobusGFSFileDebugEnter();

    buf_info1 = (globus_l_buffer_info_t *) priority_1;
    buf_info2 = (globus_l_buffer_info_t *) priority_2;
    
    /* the void * are really just offsets */
    if(buf_info1->offset > buf_info2->offset)
    {
        rc = 1;
    }
    if(buf_info1->offset < buf_info2->offset)
    {
        rc = -1;
    }
    
    GlobusGFSFileDebugExit();
    return rc;
}

static
globus_result_t
globus_l_gfs_file_monitor_init(
    globus_l_file_monitor_t **          u_monitor,
    globus_size_t                       block_size,
    int                                 optimal_count)
{
    globus_l_file_monitor_t *           monitor;
    globus_result_t                     result;
    int                                 rc;
    GlobusGFSName(globus_l_gfs_file_monitor_init);
    GlobusGFSFileDebugEnter();
        
    monitor = (globus_l_file_monitor_t *) globus_malloc(
        sizeof(globus_l_file_monitor_t));
    if(!monitor)
    {
        result = GlobusGFSErrorMemory("monitor");
        goto error_alloc;
    }
       
    rc = globus_memory_init(&monitor->mem, block_size, optimal_count);
    if(!rc)
    {
        globus_free(monitor);
        result = GlobusGFSErrorMemory("buffer");
        goto error_alloc;
    } 
    
    globus_mutex_init(&monitor->lock, NULL);
    globus_priority_q_init(
        &monitor->queue, globus_l_gfs_file_queue_compare);
    monitor->buffer_list = NULL;
    monitor->op = NULL;
    monitor->file_handle = NULL;
    monitor->pending_reads = 0;
    monitor->pending_writes = 0;
    monitor->file_offset = 0;
    monitor->block_size = block_size;
    monitor->optimal_count = optimal_count;
    monitor->error = NULL;
    monitor->eof = GLOBUS_FALSE;
    monitor->aborted = GLOBUS_FALSE;
    monitor->concurrency_check = 2;
    monitor->concurrency_check_interval = 2;
    monitor->expected_cksm = NULL;
    monitor->expected_cksm_alg = NULL;
    monitor->utime = -1;
    monitor->pathname = NULL;

    *u_monitor = monitor;
    
    GlobusGFSFileDebugExit();
    return GLOBUS_SUCCESS;

error_alloc:
    GlobusGFSFileDebugExitWithError();
    return result;
}

static
void
globus_l_gfs_file_monitor_destroy(
    globus_l_file_monitor_t *           monitor)
{
    globus_l_buffer_info_t *            buf_info;
    globus_list_t *                     list;
    globus_byte_t *                     buffer;
    GlobusGFSName(globus_l_gfs_file_monitor_destroy);
    GlobusGFSFileDebugEnter();
    
    while(!globus_priority_q_empty(&monitor->queue))
    {
        buf_info = (globus_l_buffer_info_t *)
            globus_priority_q_dequeue(&monitor->queue);
        if(buf_info)
        {
            if(buf_info->buffer)
            {
                globus_memory_push_node(&monitor->mem, buf_info->buffer);
            }
            globus_free(buf_info);
        }
    }
    
    for(list = monitor->buffer_list;
        !globus_list_empty(list);
        list = globus_list_rest(list))
    {
        buffer = (globus_byte_t *) globus_list_first(list);
        globus_memory_push_node(&monitor->mem, buffer);
    }
    
    if(monitor->pathname)
    {
        globus_free(monitor->pathname);
    }
    
    if(monitor->expected_cksm)
    {
        globus_free(monitor->expected_cksm);
    }
    if(monitor->expected_cksm_alg)
    {
        globus_free(monitor->expected_cksm_alg);
    }
    
    globus_priority_q_destroy(&monitor->queue);
    globus_list_free(monitor->buffer_list);
    globus_memory_destroy(&monitor->mem);
    globus_mutex_destroy(&monitor->lock);
    globus_free(monitor);

    GlobusGFSFileDebugExit();
}


static
void
globus_l_gfs_file_cksm_verify(
    globus_result_t                     result,
    char *                              cksm,
    void *                              user_arg)
{
    globus_l_file_monitor_t *           monitor;
    GlobusGFSName(globus_l_gfs_file_cksm_verify);
    GlobusGFSFileDebugEnter();
    
    monitor = (globus_l_file_monitor_t *) user_arg;
    
    if(result != GLOBUS_SUCCESS)
    {
        monitor->finish_result = 
            GlobusGFSErrorWrapFailed("checksum verification", result);
    }
    else if(strcmp(monitor->expected_cksm, cksm) != 0)
    {
        monitor->finish_result = GlobusGFSErrorIncorrectChecksum(
                cksm, monitor->expected_cksm);
    }

    globus_gridftp_server_finished_transfer(
        monitor->op, monitor->finish_result);
    
    globus_l_gfs_file_monitor_destroy(monitor);

    GlobusGFSFileDebugExit();
}

static 
globus_bool_t
globus_l_gfs_file_timeout_cb(
    globus_xio_handle_t                 handle,
    globus_xio_operation_type_t         type,
    void *                              user_arg)
{
    GlobusGFSName(globus_l_gfs_file_timeout_cb);

    GlobusGFSFileDebugEnter();

    globus_gfs_log_message(
        GLOBUS_GFS_LOG_WARN,
        "A file access timeout has occurred.\n");
   
    GlobusGFSFileDebugExit();

    return GLOBUS_TRUE;
}

static
void
globus_l_gfs_file_close_cb(
    globus_xio_handle_t                 handle,
    globus_result_t                     result,
    void *                              user_arg)
{
    globus_l_file_monitor_t *           monitor;
    GlobusGFSName(globus_l_gfs_file_close_cb);

    monitor = (globus_l_file_monitor_t *) user_arg;

    GlobusGFSFileDebugEnter();
    
    if(monitor != NULL)
    {
        /* must lock/unlock or we can get to the destroy before the 
           registeration locak has actually released... crazy threads */
        globus_mutex_lock(&monitor->lock);
        {
            if(monitor->finish_result == GLOBUS_SUCCESS)
            {
                monitor->finish_result = result;
            }
        }
        globus_mutex_unlock(&monitor->lock);

        if(monitor->finish_result == GLOBUS_SUCCESS && 
            monitor->utime >= 0)
        {
            monitor->finish_result = globus_l_gfs_file_utime(
                NULL, monitor->pathname, monitor->utime);
        }
        
        if(monitor->finish_result == GLOBUS_SUCCESS && 
            monitor->expected_cksm != NULL)
        {
            /* verify file before finishing */
            result = globus_l_gfs_file_cksm(
                NULL, 
                monitor->pathname, 
                monitor->expected_cksm_alg,
                0,
                -1,
                globus_l_gfs_file_cksm_verify,
                monitor);
        }
        else
        {
            globus_gridftp_server_finished_transfer(
                monitor->op, monitor->finish_result);
        
            globus_l_gfs_file_monitor_destroy(monitor);
        }
    }

    GlobusGFSFileDebugExit();
}

static
void
globus_l_gfs_file_close_kickout(
    void *                              arg)
{
    globus_l_file_monitor_t *           monitor;

    monitor = (globus_l_file_monitor_t *) arg;

    globus_gridftp_server_finished_transfer(
        monitor->op, monitor->finish_result);

    globus_l_gfs_file_monitor_destroy(monitor);
}

static
void
globus_l_gfs_file_close(
    globus_l_file_monitor_t *           monitor,
    globus_result_t                     in_result)
{
    globus_bool_t                       oneshot = GLOBUS_FALSE;
    globus_result_t                     result;

    monitor->finish_result = in_result;
    if(monitor->file_handle)
    {
        result = globus_xio_register_close(
            monitor->file_handle,
            NULL,
            globus_l_gfs_file_close_cb,
            monitor);
        if(result != GLOBUS_SUCCESS)
        {
            oneshot = GLOBUS_TRUE;

            if(monitor->finish_result == GLOBUS_SUCCESS)
            {
                monitor->finish_result = result;
            }
        }
    }
    else
    {
        oneshot = GLOBUS_TRUE;
    }

    if(oneshot)
    {
        globus_callback_register_oneshot(
            NULL,
            NULL,
            globus_l_gfs_file_close_kickout,
            monitor);
    }
}

/**
 * stat calls
 */
 
/* basepath and filename must be MAXPATHLEN long 
 * the pathname may be absolute or relative, basepath will be the same */
static
void
globus_l_gfs_file_partition_path(
    const char *                        pathname,
    char *                              basepath,
    char *                              filename)
{
    char                                buf[MAXPATHLEN];
    char *                              filepart;
    GlobusGFSName(globus_l_gfs_file_partition_path);
    GlobusGFSFileDebugEnter();
    
    strncpy(buf, pathname, MAXPATHLEN);
    buf[MAXPATHLEN - 1] = '\0';
    
    filepart = strrchr(buf, '/');
    
#ifdef WIN32


#else
    while(filepart && !*(filepart + 1) && filepart != buf)
    {
        *filepart = '\0';
        filepart = strrchr(buf, '/');
    }
#endif

    if(!filepart)
    {
        strcpy(filename, buf);
        basepath[0] = '\0';
    }
    else
    {
        if(filepart == buf)
        {
            if(!*(filepart + 1))
            {
                basepath[0] = '\0';
                filename[0] = '/';
                filename[1] = '\0';
            }
            else
            {
                *filepart++ = '\0';
                basepath[0] = '/';
                basepath[1] = '\0';
                strcpy(filename, filepart);
            }
        }
        else
        {                
            *filepart++ = '\0';
            strcpy(basepath, buf);
            strcpy(filename, filepart);
        }
    }    

    if(filename[0] == 0)
    { 
        filename[0] = '/';
        filename[1] = 0;
    }
    GlobusGFSFileDebugExit();
}

static
globus_result_t
globus_l_gfs_file_realpath(
    const char *                        in_path,
    char **                             out_path,
    void *                              user_arg)
{
    globus_result_t                     result;
    char                                resolved[MAXPATHLEN];
    GlobusGFSName(globus_l_gfs_file_stat);
    GlobusGFSFileDebugEnter();
    
    if(realpath(in_path, resolved) == NULL)
    {
        result = GlobusGFSErrorSystemError("realpath", errno);
        goto error;
    }
    *out_path = globus_libc_strdup(resolved);

    GlobusGFSFileDebugExit();
    return GLOBUS_SUCCESS;

error:
    GlobusGFSFileDebugExitWithError();
    return result;
}

static
void
globus_l_gfs_file_copy_stat(
    globus_gfs_stat_t *                 stat_object,
    struct stat *                       stat_buf,
    const char *                        filename,
    const char *                        symlink_target,
    int                                 link_mode,
    globus_gridftp_server_control_stat_error_t  error)
{
    GlobusGFSName(globus_l_gfs_file_copy_stat);
    GlobusGFSFileDebugEnter();

    stat_object->mode     = stat_buf->st_mode;
    stat_object->nlink    = stat_buf->st_nlink;
    stat_object->uid      = stat_buf->st_uid;
    stat_object->gid      = stat_buf->st_gid;
    stat_object->size     = stat_buf->st_size;
    stat_object->mtime    = stat_buf->st_mtime;
    stat_object->atime    = stat_buf->st_atime;
    stat_object->ctime    = stat_buf->st_ctime;
    stat_object->dev      = stat_buf->st_dev;
    stat_object->ino      = stat_buf->st_ino;
    stat_object->link_mode = link_mode;
    stat_object->error    = error;
    
    if(filename && *filename)
    {
        stat_object->name = globus_libc_strdup(filename);
    }
    else
    {
        stat_object->name = NULL;
    }
    if(symlink_target && *symlink_target)
    {
        stat_object->symlink_target = globus_libc_strdup(symlink_target);
    }
    else
    {
        stat_object->symlink_target = NULL;
    }

    GlobusGFSFileDebugExit();
}
static
void
globus_l_gfs_file_destroy_stat(
    globus_gfs_stat_t *                 stat_array,
    int                                 stat_count)
{
    int                                 i;
    GlobusGFSName(globus_l_gfs_file_destroy_stat);
    GlobusGFSFileDebugEnter();
        
    for(i = 0; i < stat_count; i++)
    {
        if(stat_array[i].name != NULL)
        {
            globus_free(stat_array[i].name);
        }        
        if(stat_array[i].symlink_target != NULL)
        {
            globus_free(stat_array[i].symlink_target);
        }
    }
    globus_free(stat_array);     

    GlobusGFSFileDebugExit();
}
    

#define GFS_STAT_COUNT_CHECK 100
#define GFS_STAT_COUNT_MAX 1000
#define GFS_STAT_TIME 10

static
void
globus_l_gfs_file_stat(
    globus_gfs_operation_t              op,
    globus_gfs_stat_info_t *            stat_info,
    void *                              user_arg)
{
    globus_result_t                     result = GLOBUS_SUCCESS;
    struct stat                         stat_buf;
    struct stat                         link_stat_buf;
    globus_gfs_stat_t *                 stat_array;
    int                                 stat_count = 0;
    int                                 total_stat_count = 0;
    DIR *                               dir;
    char                                basepath[MAXPATHLEN];
    char                                filename[MAXPATHLEN];
    char                                symlink_target[MAXPATHLEN];
    globus_gridftp_server_control_stat_error_t  base_error = GLOBUS_GRIDFTP_SERVER_CONTROL_STAT_SUCCESS;
    
    GlobusGFSName(globus_l_gfs_file_stat);
    GlobusGFSFileDebugEnter();
    
    /* lstat is the same as stat when not operating on a link */
    if(lstat(stat_info->pathname, &stat_buf) != 0)
    {
        result = GlobusGFSErrorSystemError("stat", errno);
        goto error_stat1;
    }
    /* if this is a link we still need to stat to get the info we are 
        interested in and then use realpath() to get the full path of 
        the symlink target */
    *symlink_target = '\0';
    if(S_ISLNK(stat_buf.st_mode))
    {
        int stat_result = 0;
        
        if(stat_info->use_symlink_info)
        {
            memset(&link_stat_buf, 0, sizeof(struct stat));
            stat_result = stat(stat_info->pathname, &link_stat_buf);
        } 
        else if(stat(stat_info->pathname, &stat_buf) != 0)
        {
            result = GlobusGFSErrorSystemError("stat", errno);
            goto error_stat1;
        }
        
        if(stat_result < 0 || realpath(stat_info->pathname, symlink_target) == NULL)
        {
            int nchars = readlink(stat_info->pathname, symlink_target, MAXPATHLEN);
            if (nchars < 0) 
        {
            result = GlobusGFSErrorSystemError("realpath", errno);
            goto error_stat1;
        }
            symlink_target[nchars] = '\0';
            base_error = GLOBUS_GRIDFTP_SERVER_CONTROL_STAT_INVALIDLINK;
        }
    }    
    globus_l_gfs_file_partition_path(stat_info->pathname, basepath, filename);
    
    if(!S_ISDIR(stat_buf.st_mode) || stat_info->file_only)
    {
        stat_array = (globus_gfs_stat_t *)
            globus_malloc(sizeof(globus_gfs_stat_t));
        if(!stat_array)
        {
            result = GlobusGFSErrorMemory("stat_array");
            goto error_alloc1;
        }
        
        globus_l_gfs_file_copy_stat(
            stat_array, &stat_buf, filename, symlink_target, link_stat_buf.st_mode, base_error);
        stat_count = 1;
    }
    else
#ifdef WIN32
    {
        /* use larger path bufs so we have the full name for err msg */
        int                             maxpathlen = GLOBUS_MAX(4096, MAXPATHLEN);
        struct dirent *                 dir_entry;
        int                             i;
        char                            dir_path[maxpathlen];
        int                             stat_limit_check = GFS_STAT_COUNT_CHECK;
        int                             stat_limit_max = GFS_STAT_COUNT_MAX;
        time_t                          stat_limit_time;
        globus_bool_t                   check_cdir = GLOBUS_TRUE;

        stat_limit_time = time(NULL) + GFS_STAT_TIME;
    
        stat_count = stat_info->include_path_stat ? 1 : 0;

    
        {
            if(stat_info->pathname && 
            stat_info->pathname[0] == '/' && stat_info->pathname[1] == 0)
            {
                DWORD drivemask;
                char drive[] = "A";
                
                stat_array = (globus_gfs_stat_t *)
                    globus_malloc(sizeof(globus_gfs_stat_t) * 30);
                if(!stat_array)
                {
                    result = GlobusGFSErrorMemory("stat_array");
                    goto error_alloc2;
                }

                drivemask = GetLogicalDrives();
                while(drivemask && *drive <= 'Z')
                {
                    if(drivemask & 1)
                    {
                        stat_buf.st_dev = *drive;
                        globus_l_gfs_file_copy_stat(&stat_array[stat_count], &stat_buf, drive, NULL, 0, 0);
                        stat_count++;
                    }
                    (*drive)++;
                    drivemask >>= 1;
                }

                goto done_fake;
                
            }
        }


        dir = globus_libc_opendir(stat_info->pathname);
        if(!dir)
        {
            result = GlobusGFSErrorSystemError("opendir", errno);
            if (!stat_info->include_path_stat)
            goto error_open;
        }
        
        total_stat_count = 0;

        while(globus_libc_readdir_r(dir, &dir_entry) == 0 && dir_entry)
        {
            total_stat_count++;
            globus_free(dir_entry);
        }
        
        globus_libc_rewinddir(dir);

        stat_array = (globus_gfs_stat_t *) globus_malloc(
            sizeof(globus_gfs_stat_t) * 
            (GLOBUS_MIN(stat_limit_max, total_stat_count) + 1));
        if(!stat_array)
        {
            result = GlobusGFSErrorMemory("stat_array");
            goto error_alloc2;
        }
        
        snprintf(
            dir_path, 
            sizeof(dir_path), 
            "%s/%s", 
            (basepath[0] != '/' || basepath[1] != '\0') ? basepath : "", 
            filename);
            
        dir_path[maxpathlen - 1] = '\0';
        if(!basepath[0] && filename[0] == '/')
        {
            dir_path[0] = 0;
        }
        
        i = 0;
        if(stat_info->include_path_stat) 
        {
            globus_l_gfs_file_copy_stat(&stat_array[i++], &stat_buf, filename, NULL, 0,
                dir ? GLOBUS_GRIDFTP_SERVER_CONTROL_STAT_SUCCESS : GLOBUS_GRIDFTP_SERVER_CONTROL_STAT_OPENFAILED);
        }
        
        while(globus_libc_readdir_r(dir, &dir_entry) == 0 && dir_entry)
        {
            char                        path[maxpathlen];
                
            base_error = GLOBUS_GRIDFTP_SERVER_CONTROL_STAT_SUCCESS;
            snprintf(path, sizeof(path), "%s/%s", dir_path, dir_entry->d_name);
            path[maxpathlen - 1] = '\0';
        
            if(stat(path, &stat_buf) != 0)
            {
                /* stat() doesn't return a useful error */
                DWORD                   winerr;
                char                    winmsg[256];
                char *                  errmsg;

                winerr = GetLastError();
                if(!FormatMessageA(
                    FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
                    NULL, winerr, 0, winmsg, sizeof(winmsg), NULL))
                {
                    sprintf(winmsg, "An unknown error occurred in stat().");
                }
                errmsg = globus_common_create_string(
                    "Directory listing failed at file '%s': %s.",
                    dir_entry->d_name, winmsg);
                result = GlobusGFSErrorGeneric(errmsg);
                globus_free(errmsg);
                globus_free(dir_entry);
                stat_count = i;
                goto error_stat2;
            }

            globus_l_gfs_file_copy_stat(
                    &stat_array[i], &stat_buf, dir_entry->d_name, symlink_target, link_stat_buf.st_mode, base_error);

            /* set nlink to total files in dir for . entry */
            if(check_cdir && dir_entry->d_name && 
                dir_entry->d_name[0] == '.' && dir_entry->d_name[1] == '\0')
            {
                check_cdir = GLOBUS_FALSE;
                stat_array[i].nlink = total_stat_count;
            }
            if(stat_array[i].ino == 0)
            {
                unsigned long                       h = 0;
                char *                              key;
            
                key = path;
                while(*key)
                {
                    h = 131 * h + *key++;
                }
                stat_array[i].ino = h;
            }

            i++;
            globus_free(dir_entry);
            
            /* send updates every GFS_STAT_TIME, checked every GFS_STAT_CHECK */
            if(i >= stat_limit_check)
            {
                time_t                  tmp_time;
                globus_bool_t           send_stats = GLOBUS_FALSE;
                
                tmp_time = time(NULL);
                if(i >= stat_limit_max || tmp_time > stat_limit_time)
                {
                    send_stats = GLOBUS_TRUE;
                }
                else
                {
                    stat_limit_check += GFS_STAT_COUNT_CHECK;
                }
                
                if(send_stats)
                {
                    stat_count = i;
                    stat_limit_check = GFS_STAT_COUNT_CHECK;
                    stat_limit_time = tmp_time + GFS_STAT_TIME;

                    i = 0;
                    
                    globus_gridftp_server_finished_stat_partial(
                        op, GLOBUS_SUCCESS, stat_array, stat_count);
                        
                    globus_l_gfs_file_destroy_stat(stat_array, stat_count);
                    
                    stat_array = (globus_gfs_stat_t *) globus_malloc(
                        sizeof(globus_gfs_stat_t) * (stat_limit_max + 1));
                    if(!stat_array)
                    {
                        result = GlobusGFSErrorMemory("stat_array");
                        goto error_alloc2;
                    }
                    
                    stat_count = 0;
                }
            }                
        }
        stat_count = i;
        
        closedir(dir);
        

    }
done_fake:

#else
    {
        struct dirent *                 dir_entry;
        struct dirent **                entries = NULL;
        int                             i;
        int                             j;
        char                            dir_path[MAXPATHLEN];
        int                             stat_limit_check = GFS_STAT_COUNT_CHECK;
        int                             stat_limit_max = GFS_STAT_COUNT_MAX;
        time_t                          stat_limit_time;
        globus_bool_t                   check_cdir = GLOBUS_TRUE;
        globus_bool_t                   slow_listings = GLOBUS_FALSE;
        int                             slow_listing_thresh;

        stat_limit_time = time(NULL) + GFS_STAT_TIME;
        
        total_stat_count = scandir(
            stat_info->pathname, 
            &entries, 
            NULL, 
            (getenv("FTPNOSORT") ? NULL : alphasort));
        if(total_stat_count < 0)
        {
            result = GlobusGFSErrorSystemError("scandir", errno);
            goto error_open;
        }

        slow_listing_thresh = globus_gfs_config_get_int("slow_dirlist");
        if(slow_listing_thresh > 0 && total_stat_count > slow_listing_thresh)
        {
            slow_listings = GLOBUS_TRUE;
        }

        stat_array = (globus_gfs_stat_t *) globus_malloc(
            sizeof(globus_gfs_stat_t) * 
            (GLOBUS_MIN(stat_limit_max, total_stat_count) + 1));
        if(!stat_array)
        {
            result = GlobusGFSErrorMemory("stat_array");
            goto error_alloc2;
        }
        
        snprintf(
            dir_path, 
            sizeof(dir_path), 
            "%s/%s", 
            (basepath[0] != '/' || basepath[1] != '\0') ? basepath : "", 
            filename);
            
        dir_path[MAXPATHLEN - 1] = '\0';
        
        i = 0;
        j = 0;
        while(j < total_stat_count)
        {            
            char                        path[MAXPATHLEN];
            
            dir_entry = entries[j++];
            snprintf(path, sizeof(path), "%s/%s", dir_path, dir_entry->d_name);
            path[MAXPATHLEN - 1] = '\0';
            
            /* fake a stat response if stats are slow, d_type is valid,
             * and indicates a file or dir */
#ifndef _DIRENT_HAVE_D_TYPE
            globus_gfs_log_message(
                GLOBUS_GFS_LOG_WARN,
                "Slow listing behavior enabled but system does not support it.");
#else
            if(slow_listings &&
                (dir_entry->d_type == DT_DIR || dir_entry->d_type == DT_REG))
            {
                unsigned long                       h = 0;
                char *                              key;

                stat_buf = (struct stat)
                {
                    .st_mode = S_IRWXU |
                        ((dir_entry->d_type == DT_DIR) ? S_IFDIR : S_IFREG),
                    .st_size = 1,
                    .st_mtime = -1,
                    .st_atime = -1,
                    .st_ctime = -1,
                    .st_dev = 1,
                    .st_ino = dir_entry->d_ino,
                    .st_nlink = 1,
                };
            }
            else
#endif
            {
                /* lstat is the same as stat when not operating on a link */
                if(lstat(path, &stat_buf) != 0)
                {
                    result = GlobusGFSErrorSystemError("lstat", errno);
                    globus_free(dir_entry);
                    /* just skip invalid entries */
                    continue;
                }
                /* if this is a link we still need to stat to get the info we are
                    interested in and then use realpath() to get the full path of
                    the symlink target */
                *symlink_target = '\0';
                if(S_ISLNK(stat_buf.st_mode))
                {
                    int stat_result = 0;

                    if(stat_info->use_symlink_info)
                    {
                        memset(&link_stat_buf, 0, sizeof(struct stat));
                        stat_result = stat(path, &link_stat_buf);
                    }
                    else if(stat(path, &stat_buf) != 0)
                    {
                        globus_free(dir_entry);
                        /* just skip invalid entries */
                        continue;
                    }
                    if(stat_result < 0 || realpath(path, symlink_target) == NULL)
                    {
                        int nchars = readlink(path, symlink_target, MAXPATHLEN);
                        if (nchars < 0)
                        {
                            globus_free(dir_entry);
                            /* just skip invalid entries */
                            continue;
                        }
                        symlink_target[nchars] = '\0';
                        base_error = GLOBUS_GRIDFTP_SERVER_CONTROL_STAT_INVALIDLINK;
                    }
                }
            }
            globus_l_gfs_file_copy_stat(
                    &stat_array[i], &stat_buf, dir_entry->d_name, symlink_target, link_stat_buf.st_mode, base_error);
            
            /* set nlink to total files in dir for . entry */
            if(check_cdir && dir_entry->d_name && 
                dir_entry->d_name[0] == '.' && dir_entry->d_name[1] == '\0')
            {
                check_cdir = GLOBUS_FALSE;
                stat_array[i].nlink = total_stat_count;
            }

            i++;
            globus_free(dir_entry);

            /* send updates every GFS_STAT_TIME, checked every GFS_STAT_CHECK
             * unless config is set for a slow listing filesystem */
            if(slow_listings || i >= stat_limit_check)
            {
                time_t                  tmp_time;
                globus_bool_t           send_stats = GLOBUS_FALSE;
                
                tmp_time = time(NULL);
                if(i >= stat_limit_max || tmp_time >= stat_limit_time)
                {
                    send_stats = GLOBUS_TRUE;
                }
                else
                {
                    stat_limit_check += GFS_STAT_COUNT_CHECK;
                }
                
                if(send_stats)
                {
                    stat_count = i;
                    stat_limit_check = GFS_STAT_COUNT_CHECK;
                    stat_limit_time = tmp_time + GFS_STAT_TIME;

                    i = 0;

                    globus_gridftp_server_finished_stat_partial(
                        op, GLOBUS_SUCCESS, stat_array, stat_count);

                    globus_l_gfs_file_destroy_stat(stat_array, stat_count);
                    
                    stat_array = (globus_gfs_stat_t *) globus_malloc(
                        sizeof(globus_gfs_stat_t) * (stat_limit_max + 1));
                    if(!stat_array)
                    {
                        result = GlobusGFSErrorMemory("stat_array");
                        goto error_alloc2;
                    }
                    
                    stat_count = 0;
                }
            }                
        }
        stat_count = i;
        
        if(entries)
        {
            globus_free(entries);
        }
    }
#endif

    globus_gridftp_server_finished_stat(
        op, result, stat_array, stat_count);
    globus_l_gfs_file_destroy_stat(stat_array, stat_count);
    
    GlobusGFSFileDebugExit();
    return;
error_stat2:
    globus_l_gfs_file_destroy_stat(stat_array, stat_count);
error_alloc2:
    closedir(dir);
    
error_open:
error_alloc1:
error_stat1:
    globus_gridftp_server_finished_stat(op, result, NULL, 0);

    GlobusGFSFileDebugExitWithError();
}


static
globus_result_t
globus_l_gfs_file_truncate(
    globus_gfs_operation_t              op,
    const char *                        pathname,
    globus_off_t                        offset)
{
    int                                 rc;
    globus_result_t                     result;
    globus_xio_system_file_t            fd;
    struct stat                         sbuf;
    
    GlobusGFSName(globus_l_gfs_file_truncate);
    GlobusGFSFileDebugEnter();

    result = globus_xio_system_file_open(
        &fd, pathname, 
        GLOBUS_XIO_FILE_RDWR | GLOBUS_XIO_FILE_BINARY, 0);
    if(result != GLOBUS_SUCCESS)
    {
        goto error;
    }

#ifdef WIN32
    rc = stat(pathname, &sbuf);    
#else
    rc = fstat(fd, &sbuf);
#endif    
    if(rc != 0)
    {
        result = GlobusGFSErrorSystemError("stat", errno);
        goto error_close;
    }
    
    if(offset > sbuf.st_size)
    {
        result = GlobusGFSErrorGeneric(
            "Current size is smaller than truncate size.");
        goto error_close;
    }

    result = globus_xio_system_file_truncate(fd, offset);
    if(result != GLOBUS_SUCCESS)
    {
        goto error_close;
    }
    
    result = globus_xio_system_file_close(fd);
    if(result != GLOBUS_SUCCESS)
    {
        goto error;
    }
        
    globus_gridftp_server_finished_command(op, GLOBUS_SUCCESS, NULL);
        
    GlobusGFSFileDebugExit();
    return GLOBUS_SUCCESS;

error_close:
    globus_xio_system_file_close(fd);
error:
    GlobusGFSFileDebugExitWithError();
    return result;
}

static
globus_result_t
globus_l_gfs_file_mkdir(
    globus_gfs_operation_t   op,
    const char *                        pathname)
{
    int                                 rc;
    globus_result_t                     result;
    GlobusGFSName(globus_l_gfs_file_mkdir);
    GlobusGFSFileDebugEnter();

    rc = mkdir(pathname, 0777);
    if(rc != 0)
    {
        result = GlobusGFSErrorSystemError("mkdir", errno);
        goto error;
    }
    
    globus_gridftp_server_finished_command(op, GLOBUS_SUCCESS, NULL);
        
    GlobusGFSFileDebugExit();
    return GLOBUS_SUCCESS;
    
error:
    GlobusGFSFileDebugExitWithError();
    return result;
}

static
globus_result_t
globus_l_gfs_file_rmdir(
    globus_gfs_operation_t   op,
    const char *                        pathname)
{
    int                                 rc;
    globus_result_t                     result;
    GlobusGFSName(globus_l_gfs_file_rmdir);
    GlobusGFSFileDebugEnter();

    rc = rmdir(pathname);
    if(rc != 0)
    {
        result = GlobusGFSErrorSystemError("rmdir", errno);
        goto error;
    }
    
    globus_gridftp_server_finished_command(op, GLOBUS_SUCCESS, NULL);
        
    GlobusGFSFileDebugExit();
    return GLOBUS_SUCCESS;
    
error:
    GlobusGFSFileDebugExitWithError();
    return result;
}

static
globus_result_t
globus_l_gfs_file_delete_dir(
    const char *                        pathname)
{

    globus_result_t                     result;
    int                                 rc;
    DIR *                               dir;
    struct stat                         stat_buf;
    struct dirent *                     dir_entry;
    int                                 i;
    char                                path[MAXPATHLEN];
    GlobusGFSName(globus_l_gfs_file_delete_dir);
    GlobusGFSFileDebugEnter();
    
    /* lstat is the same as stat when not operating on a link */
    if(lstat(pathname, &stat_buf) != 0)
    {
        result = GlobusGFSErrorSystemError("stat", errno);
        goto error_stat;
    }
    
    if(!S_ISDIR(stat_buf.st_mode))
    {
        /* remove anything that isn't a dir -- don't follow links */
        rc = unlink(pathname);       
        if(rc != 0)
        {
            result = GlobusGFSErrorSystemError("unlink", errno);
            goto error_unlink1;
        }
    }
    else
    {
        dir = globus_libc_opendir(pathname);
        if(!dir)
        {
            result = GlobusGFSErrorSystemError("opendir", errno);
            goto error_open;
        }
        
        for(i = 0;
            globus_libc_readdir_r(dir, &dir_entry) == 0 && dir_entry;
            i++)
        {   
            if(dir_entry->d_name[0] == '.' && 
                (dir_entry->d_name[1] == '\0' || 
                (dir_entry->d_name[1] == '.' && dir_entry->d_name[2] == '\0')))
            {
                globus_free(dir_entry);
                continue;
            }
            snprintf(path, sizeof(path), "%s/%s", pathname, dir_entry->d_name);
            path[MAXPATHLEN - 1] = '\0';
              
            /* lstat is the same as stat when not operating on a link */
            if(lstat(path, &stat_buf) != 0)
            {
                result = GlobusGFSErrorSystemError("lstat", errno);
                globus_free(dir_entry);
                /* just skip invalid entries */
                continue;
            }
            
            if(!S_ISDIR(stat_buf.st_mode))
            {
                /* remove anything that isn't a dir -- don't follow links */
                rc = unlink(path);       
                if(rc != 0)
                {
                    result = GlobusGFSErrorSystemError("unlink", errno);
                    goto error_unlink2;
                }
            }
            else
            {
                result = globus_l_gfs_file_delete_dir(path);
                if(result != GLOBUS_SUCCESS)
                {
                    goto error_recurse;
                }
            }

            globus_free(dir_entry);
        }

        closedir(dir);
        rc = rmdir(pathname);
        if(rc != 0)
        {
            result = GlobusGFSErrorSystemError("rmdir", errno);
            goto error_rmdir;
        }
    } 
    
    GlobusGFSFileDebugExit();
    return GLOBUS_SUCCESS;

error_recurse:
error_unlink2:
        closedir(dir);
        globus_free(dir_entry);
error_open: 
error_stat:
error_unlink1:
error_rmdir:    
    GlobusGFSFileDebugExitWithError();
    return result; 
}
    
static
globus_result_t
globus_l_gfs_file_delete(
    globus_gfs_operation_t              op,
    const char *                        pathname,
    globus_bool_t                       recurse)
{
    int                                 rc;
    globus_result_t                     result;
    GlobusGFSName(globus_l_gfs_file_delete);
    GlobusGFSFileDebugEnter();

    if(!recurse)
    {
        rc = unlink(pathname);
        if(rc != 0)
        {
            result = GlobusGFSErrorSystemError("unlink", errno);
            goto error;
        }
    }
    else
    {
        result = globus_l_gfs_file_delete_dir(pathname);
        if(result != GLOBUS_SUCCESS)
        {
            result = GlobusGFSErrorWrapFailed("recursion", result);
            goto error;
        }
    }
    
    globus_gridftp_server_finished_command(op, GLOBUS_SUCCESS, NULL);
        
    GlobusGFSFileDebugExit();
    return GLOBUS_SUCCESS;
    
error:
    GlobusGFSFileDebugExitWithError();
    return result;
}

static
globus_result_t
globus_l_gfs_file_rename(
    globus_gfs_operation_t   op,
    const char *                        from_pathname,
    const char *                        to_pathname)
{
    int                                 rc;
    globus_result_t                     result;
    GlobusGFSName(globus_l_gfs_file_rename);
    GlobusGFSFileDebugEnter();

    rc = rename(from_pathname, to_pathname);
    if(rc != 0)
    {
        result = GlobusGFSErrorSystemError("rename", errno);
        goto error;
    }
    
    globus_gridftp_server_finished_command(op, GLOBUS_SUCCESS, NULL);
        
    GlobusGFSFileDebugExit();
    return GLOBUS_SUCCESS;
    
error:
    GlobusGFSFileDebugExitWithError();
    return result;
}

static
globus_result_t
globus_l_gfs_file_chgrp(
    globus_gfs_operation_t   op,
    const char *                        pathname,
    const char *                        group)
{
    int                                 rc;
    globus_result_t                     result;
    struct group *                      grp_info;
    int                                 grp_id;
    char*                               endpt;
    
    GlobusGFSName(globus_l_gfs_file_chgrp);
    GlobusGFSFileDebugEnter();

    grp_info = getgrnam(group);
    if(grp_info != NULL)
    {
        grp_id = grp_info->gr_gid;
    } 
    else
    {
        grp_id = strtol(group, &endpt, 10);
        if(*group == '\0' || *endpt != '\0')
        {
            result = GlobusGFSErrorSystemError("chgrp", EPERM);
            goto error;
        }
    }
    
    if(grp_id < 0)
    {
        result = GlobusGFSErrorSystemError("chgrp", EPERM);
        goto error;
    }
    
    rc = chown(pathname, -1, grp_id);
    if(rc != 0)
    {
        result = GlobusGFSErrorSystemError("chgrp", errno);
        goto error;
    }
    
    globus_gridftp_server_finished_command(op, GLOBUS_SUCCESS, NULL);
        
    GlobusGFSFileDebugExit();
    return GLOBUS_SUCCESS;
    
error:
    GlobusGFSFileDebugExitWithError();
    return result;
}

#ifdef WIN32

/* utime on win32 does not work with directories.
    we can work around that by opening a HANDLE with the 
    FILE_FLAG_BACKUP_SEMANTICS flag, getting the fd from that, and calling 
    _futime on that fd.

    we could call SetFileTime() with the HANDLE, but there are quirks with 
    DST that result in the time returned by stat() being different than the
    set time depending on the date and current DST state.  the perl module
    Win32-UTCFileTime documents that bit of fun.
*/
    
static BOOL 
utime_win(
    const char *                        path,
    struct utimbuf *                    ubuf)
{
    HANDLE                              hFile;
    int                                 rc;
    int                                 fd;
    hFile = CreateFile(
        path, FILE_WRITE_ATTRIBUTES, FILE_SHARE_WRITE, 0, 
        OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, 0);
    if (hFile == INVALID_HANDLE_VALUE)
    {
        errno = GetLastError();
        return -1;
    }
    fd = _open_osfhandle((intptr_t) hFile, 0);
    rc = _futime(fd, (struct _utimbuf *) ubuf);
    /* _close closes the underlying HANDLE */
    _close(fd);
    return rc;
}
#endif

static
globus_result_t
globus_l_gfs_file_utime(
    globus_gfs_operation_t              op,
    const char *                        pathname,
    time_t                              modtime)
{
    int                                 rc;
    globus_result_t                     result;
    struct utimbuf                      ubuf;
    GlobusGFSName(globus_l_gfs_file_utime);
    GlobusGFSFileDebugEnter();

    ubuf.modtime = modtime;
    ubuf.actime = time(NULL);

#ifdef WIN32
    rc = utime_win(pathname, &ubuf);
#else   
    rc = utime(pathname, &ubuf);
#endif
    if(rc != 0)
    {
        result = GlobusGFSErrorSystemError("utime", errno);
        goto error;
    }
    
    if(op)
    {
        globus_gridftp_server_finished_command(op, GLOBUS_SUCCESS, NULL);
    }
    
    GlobusGFSFileDebugExit();
    return GLOBUS_SUCCESS;
    
error:
    GlobusGFSFileDebugExitWithError();
    return result;
}

static
globus_result_t
globus_l_gfs_file_symlink(
    globus_gfs_operation_t   op,
    const char *                        reference_path,
    const char *                        pathname)
{
    int                                 rc;
    globus_result_t                     result;
    GlobusGFSName(globus_l_gfs_file_symlink);
    GlobusGFSFileDebugEnter();

    rc = symlink(reference_path, pathname);
    if(rc != 0)
    {
        result = GlobusGFSErrorSystemError("symlink", errno);
        goto error;
    }
    
    globus_gridftp_server_finished_command(op, GLOBUS_SUCCESS, NULL);
        
    GlobusGFSFileDebugExit();
    return GLOBUS_SUCCESS;
    
error:
    GlobusGFSFileDebugExitWithError();
    return result;
}

static
globus_result_t
globus_l_gfs_file_chmod(
    globus_gfs_operation_t   op,
    const char *                        pathname,
    mode_t                              mode)
{
    int                                 rc;
    globus_result_t                     result;
    GlobusGFSName(globus_l_gfs_file_chmod);
    GlobusGFSFileDebugEnter();

    rc = chmod(pathname, mode);
    if(rc != 0)
    {
        result = GlobusGFSErrorSystemError("chmod", errno);
        goto error;
    }
    
    globus_gridftp_server_finished_command(op, GLOBUS_SUCCESS, NULL);
        
    GlobusGFSFileDebugExit();
    return GLOBUS_SUCCESS;
    
error:
    GlobusGFSFileDebugExitWithError();
    return result;
}

static
void
globus_l_gfs_file_cksm_read_cb(
    globus_xio_handle_t                 handle, 
    globus_result_t                     result,
    globus_byte_t *                     buffer,
    globus_size_t                       len,
    globus_size_t                       nbytes, 
    globus_xio_data_descriptor_t        data_desc,
    void *                              user_arg)
{
    globus_l_gfs_file_cksm_monitor_t *  monitor;
    globus_bool_t                       eof = GLOBUS_FALSE;
    char *                              cksmptr = NULL;
    char *                              md5ptr;
    char *                              shaptr;
    unsigned char                       md[MD5_DIGEST_LENGTH];
    char                                md5sum[MD5_DIGEST_LENGTH * 2 + 1] = {0};
    unsigned char                       sha[SHA512_DIGEST_LENGTH];
    char                                shasum[SHA512_DIGEST_LENGTH * 2 + 1] = {0};
    char                                adler32_human[2*sizeof(uint32_t)+1];
    int                                 i;    

    GlobusGFSName(globus_l_gfs_file_cksm_read_cb);
    GlobusGFSFileDebugEnter();
    
    monitor = (globus_l_gfs_file_cksm_monitor_t *) user_arg;

    if(result != GLOBUS_SUCCESS)
    {
        if(globus_xio_error_is_eof(result))
        {
            eof = GLOBUS_TRUE;
        }
        else
        {
            result = GlobusGFSErrorWrapFailed(
                "checksum read callback", result);
            goto error_read;
        }
    }        
    if(monitor->length >= 0)
    {
        monitor->read_left -= nbytes;
        monitor->count = (monitor->read_left > monitor->block_size) ? 
            monitor->block_size : monitor->read_left;
        if(monitor->read_left == 0)
        {
            eof = GLOBUS_TRUE;
        }
    }
    monitor->total_bytes += nbytes;

    if(monitor->cksum_type == GLOBUS_GFS_FILE_CKSM_TYPE_MD5)
    {
        MD5_Update(&monitor->mdctx, buffer, nbytes);
    }
    else if (monitor->cksum_type == GLOBUS_GFS_FILE_CKSM_TYPE_SHA1)
    {
        SHA1_Update(&monitor->sha1ctx, buffer, nbytes);
    }
    else if (monitor->cksum_type == GLOBUS_GFS_FILE_CKSM_TYPE_SHA256)
    {
        SHA256_Update(&monitor->sha256ctx, buffer, nbytes);
    }
    else if (monitor->cksum_type == GLOBUS_GFS_FILE_CKSM_TYPE_SHA512)
    {
        SHA512_Update(&monitor->sha512ctx, buffer, nbytes);
    }
    else if (monitor->cksum_type == GLOBUS_GFS_FILE_CKSM_TYPE_ADLER32)
    {
        monitor->adler32ctx = adler32(monitor->adler32ctx, buffer, nbytes);
    }

    if(!eof)
    {
        if(monitor->send_marker)
        {
            monitor->send_marker = GLOBUS_FALSE;
            
            char                        count[128];
            sprintf(count, "%"GLOBUS_OFF_T_FORMAT, monitor->total_bytes);
            
            globus_gridftp_server_intermediate_command(
                monitor->op, GLOBUS_SUCCESS, count);
        }

        if (monitor->delay_read)
        {
            nanosleep(&(struct timespec) {.tv_nsec = monitor->delay_read * 1000}, NULL);
        }

        result = globus_xio_register_read(
            handle,
            monitor->buffer,
            monitor->count,
            monitor->count,
            NULL,
            globus_l_gfs_file_cksm_read_cb,
            monitor);
        if(result != GLOBUS_SUCCESS)
        {
            result = GlobusGFSErrorWrapFailed(
                "globus_xio_register_read", result);
            goto error_register;
        }
    }
    else
    {
        if(monitor->marker_handle)
        {
            globus_callback_unregister(
                monitor->marker_handle,
                NULL,
                NULL,
                NULL);
            monitor->marker_handle = GLOBUS_NULL_HANDLE;
        }
        
        globus_xio_register_close(
            handle,
            NULL,
            globus_l_gfs_file_close_cb,
            NULL);

        if (monitor->cksum_type == GLOBUS_GFS_FILE_CKSM_TYPE_MD5)
        {
            MD5_Final(md, &monitor->mdctx);
            md5ptr = md5sum;
            for(i = 0; i < MD5_DIGEST_LENGTH; i++)
            {
               md5ptr += sprintf(md5ptr, "%02x", md[i]);
            }
            cksmptr = md5sum;
        }
        else if (monitor->cksum_type == GLOBUS_GFS_FILE_CKSM_TYPE_SHA1)
        {
            SHA1_Final(sha, &monitor->sha1ctx);
            shaptr = shasum;
            for(i = 0; i < SHA_DIGEST_LENGTH; i++)
            {
               shaptr += sprintf(shaptr, "%02x", sha[i]);
            }
            cksmptr = shasum;
        }
        else if (monitor->cksum_type == GLOBUS_GFS_FILE_CKSM_TYPE_SHA256)
        {
            SHA256_Final(sha, &monitor->sha256ctx);
            shaptr = shasum;
            for(i = 0; i < SHA256_DIGEST_LENGTH; i++)
            {
               shaptr += sprintf(shaptr, "%02x", sha[i]);
            }
            cksmptr = shasum;
        }
        else if (monitor->cksum_type == GLOBUS_GFS_FILE_CKSM_TYPE_SHA512)
        {
            SHA512_Final(sha, &monitor->sha512ctx);
            shaptr = shasum;
            for(i = 0; i < SHA512_DIGEST_LENGTH; i++)
            {
               shaptr += sprintf(shaptr, "%02x", sha[i]);
            }
            cksmptr = shasum;
        }
        else if (monitor->cksum_type == GLOBUS_GFS_FILE_CKSM_TYPE_ADLER32)
        {
            snprintf(adler32_human, sizeof(adler32_human),
                "%08x", monitor->adler32ctx);
            cksmptr = adler32_human;
        }

        if(monitor->internal_cb)
        {
            monitor->internal_cb(
                GLOBUS_SUCCESS, cksmptr, monitor->internal_cb_arg);
        }
        else
        {
            globus_gridftp_server_finished_command(
                monitor->op, GLOBUS_SUCCESS, cksmptr);
        }   
        
        globus_free(monitor);
            
    }        
    GlobusGFSFileDebugExit();
    return;
        

error_register:
error_read:
    globus_xio_register_close(handle, NULL, NULL, NULL);
    handle = NULL;
    globus_gridftp_server_finished_command(monitor->op, result, NULL);    
    globus_free(monitor);
    
    GlobusGFSFileDebugExitWithError();
}


static void
globus_l_gfs_file_marker_cb(
    void *                              user_arg)
{
    globus_l_gfs_file_cksm_monitor_t *  monitor;
    
    monitor = (globus_l_gfs_file_cksm_monitor_t *) user_arg;
    monitor->send_marker = GLOBUS_TRUE;
}

static
void
globus_l_gfs_file_open_cksm_cb(
    globus_xio_handle_t                 handle,
    globus_result_t                     result,
    void *                              user_arg)
{  
    globus_l_gfs_file_cksm_monitor_t *  monitor;
    char *                              freq;
    GlobusGFSName(globus_l_gfs_file_open_cksm_cb);
    GlobusGFSFileDebugEnter();
    
    monitor = (globus_l_gfs_file_cksm_monitor_t *) user_arg;

    if(result != GLOBUS_SUCCESS)
    {
        result = GlobusGFSErrorWrapFailed(
            "open", result);
        goto error_open;  
    }  
    
    if(monitor->op)
    {
        globus_gridftp_server_get_update_interval(
            monitor->op, &monitor->marker_freq);
    }

    if(monitor->marker_freq)
    {
        globus_result_t                     res;
        globus_reltime_t                    delay;
        
        GlobusTimeReltimeSet(delay, monitor->marker_freq, 0);
        res = globus_callback_register_periodic(
            &monitor->marker_handle,
            &delay,
            &delay,
            globus_l_gfs_file_marker_cb,
            monitor);
        if(res != GLOBUS_SUCCESS)
        {
            
        }
    }
    
    if(monitor->length >= 0)
    {
        monitor->read_left = monitor->length;
        monitor->count = (monitor->read_left > monitor->block_size) ? 
            monitor->block_size : monitor->read_left;
    }
    else
    {
        monitor->count = monitor->block_size;
    }
    
    if(monitor->offset > 0)
    {
        result = globus_xio_handle_cntl(
            handle,
            GLOBUS_XIO_QUERY,
            GLOBUS_XIO_SEEK,
            monitor->offset,
            GLOBUS_XIO_FILE_SEEK_SET);
        if(result != GLOBUS_SUCCESS)
        {
            result = GlobusGFSErrorWrapFailed(
                "globus_xio_handle_cntl", result);
            goto error_seek;
        }
    }

    if(monitor->cksum_type == GLOBUS_GFS_FILE_CKSM_TYPE_MD5)
    {
        MD5_Init(&monitor->mdctx);
    }
    else if (monitor->cksum_type == GLOBUS_GFS_FILE_CKSM_TYPE_SHA1)
    {
        SHA1_Init(&monitor->sha1ctx);
    }
    else if (monitor->cksum_type == GLOBUS_GFS_FILE_CKSM_TYPE_SHA256)
    {
        SHA256_Init(&monitor->sha256ctx);
    }
    else if (monitor->cksum_type == GLOBUS_GFS_FILE_CKSM_TYPE_SHA512)
    {
        SHA512_Init(&monitor->sha512ctx);
    }
    else if (monitor->cksum_type == GLOBUS_GFS_FILE_CKSM_TYPE_ADLER32)
    {
        monitor->adler32ctx = adler32(0, NULL, 0);
    }
    
    
    result = globus_xio_register_read(
        handle,
        monitor->buffer,
        monitor->count,
        monitor->count,
        NULL,
        globus_l_gfs_file_cksm_read_cb,
        monitor);
    if(result != GLOBUS_SUCCESS)
    {
        result = GlobusGFSErrorWrapFailed(
            "globus_xio_register_read", result);
        goto error_register;
    }

    GlobusGFSFileDebugExit();
    return;
        
error_register:
error_seek:
error_open:
    globus_xio_register_close(handle, NULL, NULL, NULL);
    handle = NULL;
    if(monitor->internal_cb)
    {
        monitor->internal_cb(result, NULL, monitor->internal_cb_arg);
    }
    else
    {
        globus_gridftp_server_finished_command(monitor->op, result, NULL);
    }   
    globus_free(monitor);
    
    GlobusGFSFileDebugExitWithError();
}


static
globus_result_t
globus_l_gfs_file_cksm(
    globus_gfs_operation_t              op,
    const char *                        pathname,
    const char *                        algorithm,
    globus_off_t                        offset,
    globus_off_t                        length,
    globus_l_gfs_file_cksm_cb_t         internal_cb,
    void *                              internal_cb_arg)
{
    globus_result_t                     result;
    globus_xio_attr_t                   attr;
    globus_xio_stack_t                  stack;
    globus_xio_handle_t                 file_handle;
    globus_l_gfs_file_cksm_monitor_t *  monitor;
    globus_size_t                       block_size;
    int                                 timeout;
    int                                 cksm_type = 0;
    GlobusGFSName(globus_l_gfs_file_cksm);
    GlobusGFSFileDebugEnter();
    
    if(offset < 0)
    {
        result = GlobusGFSErrorGeneric("Invalid offset.");
        goto param_error;
    }

    if(!strcasecmp("md5", algorithm))
    {
        cksm_type = GLOBUS_GFS_FILE_CKSM_TYPE_MD5;
    }
    else if(!strcasecmp("sha1", algorithm))
    {
        cksm_type = GLOBUS_GFS_FILE_CKSM_TYPE_SHA1;
    }
    else if(!strcasecmp("sha256", algorithm))
    {
        cksm_type = GLOBUS_GFS_FILE_CKSM_TYPE_SHA256;
    }
    else if(!strcasecmp("sha512", algorithm))
    {
        cksm_type = GLOBUS_GFS_FILE_CKSM_TYPE_SHA512;
    }
    else if(!strcasecmp("adler32", algorithm))
    {
        cksm_type = GLOBUS_GFS_FILE_CKSM_TYPE_ADLER32;
    }
    else
    {
        result = GlobusGFSErrorGeneric("Unknown checksum algorithm requested.");
        goto alg_error;
    }

    result = globus_xio_attr_init(&attr);
    if(result != GLOBUS_SUCCESS)
    {
        result = GlobusGFSErrorWrapFailed("globus_xio_attr_init", result);
        goto error_attr;
    }

    result = globus_xio_attr_cntl(
        attr,
        globus_l_gfs_file_driver,
        GLOBUS_XIO_FILE_SET_FLAGS,
        O_RDONLY);
    if(result != GLOBUS_SUCCESS)
    {
        result = GlobusGFSErrorWrapFailed("globus_xio_attr_init", result);
        goto error_cntl;
    }

    timeout = globus_gfs_config_get_int("file_timeout");
    if(timeout > 0)
    {
        globus_reltime_t                delay;

        GlobusTimeReltimeSet(delay, timeout, 0);
        result = globus_xio_attr_cntl(
            attr,
            NULL,
            GLOBUS_XIO_ATTR_SET_TIMEOUT_ALL,
            globus_l_gfs_file_timeout_cb,
            &delay,
            NULL);
        if(result != GLOBUS_SUCCESS)
        {
            globus_gfs_log_message(
                GLOBUS_GFS_LOG_WARN,
                "Unable to set file access timeout of %d seconds\n", 
                timeout);
        }
    }
    
    result = globus_xio_stack_init(&stack, NULL);
    if(result != GLOBUS_SUCCESS)
    {
        result = GlobusGFSErrorWrapFailed("globus_xio_stack_init", result);
        goto error_stack;
    }
    
    result = globus_xio_stack_push_driver(stack, globus_l_gfs_file_driver);
    if(result != GLOBUS_SUCCESS)
    {
        result = GlobusGFSErrorWrapFailed(
            "globus_xio_stack_push_driver", result);
        goto error_push;
    }

    result = globus_xio_handle_create(&file_handle, stack);
    if(result != GLOBUS_SUCCESS)
    {
        result = GlobusGFSErrorWrapFailed("globus_xio_handle_create", result);
        goto error_create;
    }

    globus_gridftp_server_get_block_size(op, &block_size);

    monitor = (globus_l_gfs_file_cksm_monitor_t *) globus_calloc(
        1, sizeof(globus_l_gfs_file_cksm_monitor_t) + block_size);
    if(monitor == NULL)
    {
        result = GlobusGFSErrorMemory("checksum buffer");
        goto error_mem;
    }
    
    monitor->op = op;
    monitor->offset = offset;
    monitor->length = length;
    monitor->block_size = block_size;
    monitor->internal_cb = internal_cb;
    monitor->internal_cb_arg = internal_cb_arg;
    monitor->delay_read = globus_gfs_config_get_int("checksum_throttle");
    monitor->cksum_type = cksm_type;

    result = globus_xio_register_open(
        file_handle,
        pathname,
        attr,
        globus_l_gfs_file_open_cksm_cb,
        monitor);
    if(result != GLOBUS_SUCCESS)
    {
        result = GlobusGFSErrorWrapFailed("globus_xio_register_open", result);
        goto error_register;
    }

    globus_xio_attr_destroy(attr);
    globus_xio_stack_destroy(stack);
    
    GlobusGFSFileDebugExit();
    return GLOBUS_SUCCESS;

error_register:
    globus_xio_register_close(file_handle, NULL, NULL, NULL);
    file_handle = NULL;
    globus_free(monitor);
    
error_mem:
error_create:
error_push:
    globus_xio_stack_destroy(stack);
    
error_stack:
error_cntl:    
    globus_xio_attr_destroy(attr);
    
error_attr:
alg_error:
param_error:
    GlobusGFSFileDebugExitWithError();
    return result;
}     

static
void
globus_l_gfs_file_command(
    globus_gfs_operation_t   op,
    globus_gfs_command_info_t *        cmd_info,
    void *                              user_arg)
{
    globus_result_t                     result;
    GlobusGFSName(globus_l_gfs_file_command);
    GlobusGFSFileDebugEnter();

    switch(cmd_info->command)
    {
      case GLOBUS_GFS_CMD_MKD:
        result = globus_l_gfs_file_mkdir(op, cmd_info->pathname);
        break;
      case GLOBUS_GFS_CMD_RMD:
        result = globus_l_gfs_file_rmdir(op, cmd_info->pathname);
        break;
      case GLOBUS_GFS_CMD_DELE:
        result = globus_l_gfs_file_delete(
            op, cmd_info->pathname, GLOBUS_FALSE);
        break;
      case GLOBUS_GFS_CMD_TRNC:
        result = globus_l_gfs_file_truncate(
            op, cmd_info->pathname, cmd_info->cksm_offset);
        break;
      case GLOBUS_GFS_CMD_SITE_RDEL:
        result = globus_l_gfs_file_delete(
            op, cmd_info->pathname, GLOBUS_TRUE);
        break;
      case GLOBUS_GFS_CMD_RNTO:
        result = globus_l_gfs_file_rename(
            op, cmd_info->from_pathname, cmd_info->pathname);
        break;
      case GLOBUS_GFS_CMD_SITE_CHMOD:
        result = globus_l_gfs_file_chmod(
            op, cmd_info->pathname, cmd_info->chmod_mode);
        break;
      case GLOBUS_GFS_CMD_SITE_CHGRP:
        result = globus_l_gfs_file_chgrp(
            op, cmd_info->pathname, cmd_info->chgrp_group);
        break;
      case GLOBUS_GFS_CMD_SITE_UTIME:
        result = globus_l_gfs_file_utime(
            op, cmd_info->pathname, cmd_info->utime_time);
        break;
      case GLOBUS_GFS_CMD_SITE_SYMLINK:
        result = globus_l_gfs_file_symlink(
            op, cmd_info->from_pathname, cmd_info->pathname);
        break;
      case GLOBUS_GFS_CMD_CKSM:
        result = globus_l_gfs_file_cksm(
            op, 
            cmd_info->pathname, 
            cmd_info->cksm_alg,
            cmd_info->cksm_offset,
            cmd_info->cksm_length,
            NULL,
            NULL);
        break;
      
      default:
        result = GLOBUS_FAILURE;
        break;
    }

    if(result != GLOBUS_SUCCESS)
    {
        goto error;
    }
    
    GlobusGFSFileDebugExit();
    return;

error:
    globus_gridftp_server_finished_command(op, result, NULL);    

    GlobusGFSFileDebugExitWithError();
}

/**
 * recv calls
 */

static
void
globus_l_gfs_file_server_read_cb(
    globus_gfs_operation_t   op,
    globus_result_t                     result,
    globus_byte_t *                     buffer,
    globus_size_t                       nbytes,
    globus_off_t                        offset,
    globus_bool_t                       eof,
    void *                              user_arg);

static
globus_result_t
globus_l_gfs_file_dispatch_write(
    globus_l_file_monitor_t *           monitor);
    
static
void
globus_l_gfs_file_write_cb(
    globus_xio_handle_t                 xio_handle, 
    globus_result_t                     result,
    globus_byte_t *                     buffer,
    globus_size_t                       len,
    globus_size_t                       nbytes, 
    globus_xio_data_descriptor_t        data_desc,
    void *                              user_arg)
{
    globus_l_file_monitor_t *           monitor;
    GlobusGFSName(globus_l_gfs_file_write_cb);
    GlobusGFSFileDebugEnter();
    
    monitor = (globus_l_file_monitor_t *) user_arg;
    
    globus_mutex_lock(&monitor->lock);
    { 
        monitor->pending_writes--;
        globus_gridftp_server_update_bytes_written(
            monitor->op, 
            monitor->file_offset,
            nbytes);
        monitor->file_offset += nbytes;

        if(result != GLOBUS_SUCCESS && monitor->error == NULL)
        {
            monitor->error = GlobusGFSErrorObjWrapFailed("callback", result);
        }
        if(monitor->error != NULL)
        {
            goto error;
        }
        
        if(!monitor->eof)
        {
            result = globus_gridftp_server_register_read(
                monitor->op,
                buffer,
                monitor->block_size,
                globus_l_gfs_file_server_read_cb,
                monitor);
            if(result != GLOBUS_SUCCESS)
            {
                monitor->error = GlobusGFSErrorObjWrapFailed(
                    "globus_gridftp_server_register_read", result);
                goto error;
            }
            
            monitor->pending_reads++;
        }
        else
        {
            globus_memory_push_node(&monitor->mem, buffer);
        }
        
        result = globus_l_gfs_file_dispatch_write(monitor);
        if(result != GLOBUS_SUCCESS)
        {
            monitor->error = GlobusGFSErrorObjWrapFailed(
                "globus_l_gfs_file_dispatch_write", result);
            goto error_dispatch;
        }
        
        if(monitor->pending_reads == 0 && monitor->pending_writes == 0)
        {
            globus_assert(monitor->eof || monitor->aborted);

            globus_l_gfs_file_close(monitor, GLOBUS_SUCCESS);
            globus_mutex_unlock(&monitor->lock);
        }
        else
        {
            globus_mutex_unlock(&monitor->lock);
        }
    }
    /* already unlocked */
    
    GlobusGFSFileDebugExit();
    return;

error:
    globus_memory_push_node(&monitor->mem, buffer);
error_dispatch:
    if(monitor->pending_reads != 0 || monitor->pending_writes != 0)
    {
        /* there are still outstanding callbacks, wait for them */
        globus_mutex_unlock(&monitor->lock);
    
        GlobusGFSFileDebugExitWithError();
        return;
    }

    globus_l_gfs_file_close(monitor, globus_error_put(monitor->error));
    globus_mutex_unlock(&monitor->lock);

    GlobusGFSFileDebugExitWithError();
}

/* Called LOCKED */
static
globus_result_t
globus_l_gfs_file_dispatch_write(
    globus_l_file_monitor_t *           monitor)
{
    globus_l_buffer_info_t *            buf_info;
    globus_result_t                     result;
    GlobusGFSName(globus_l_gfs_file_dispatch_write);
    GlobusGFSFileDebugEnter();
    
    if(monitor->pending_writes == 0 && !monitor->aborted)
    {
        buf_info = (globus_l_buffer_info_t *)
            globus_priority_q_dequeue(&monitor->queue);
        if(buf_info)
        {
            if(buf_info->offset != monitor->file_offset)
            { 
                globus_off_t            seek_tmp;

                monitor->file_offset = buf_info->offset;
                seek_tmp = monitor->file_offset;

                result = globus_xio_handle_cntl(
                    monitor->file_handle,
                    GLOBUS_XIO_QUERY,
                    GLOBUS_XIO_SEEK,
                    seek_tmp,
                    GLOBUS_XIO_FILE_SEEK_SET);
                if(result != GLOBUS_SUCCESS)
                {
                    result = GlobusGFSErrorWrapFailed(
                        "globus_xio_handle_cntl", result);
                    goto error_seek;
                }
            }
            
            result = globus_xio_register_write(
                monitor->file_handle,
                buf_info->buffer,
                buf_info->length,
                buf_info->length,
                NULL,
                globus_l_gfs_file_write_cb,
                monitor);
            if(result != GLOBUS_SUCCESS)
            {
                result = GlobusGFSErrorWrapFailed(
                    "globus_xio_register_write", result);
                goto error_register;
            }
            
            monitor->pending_writes++;
            
            globus_free(buf_info);
        }
    }
    
    GlobusGFSFileDebugExit();
    return GLOBUS_SUCCESS;

error_register:
error_seek:
    if(buf_info->buffer)
    {
        globus_memory_push_node(&monitor->mem, buf_info->buffer);
    }
    globus_free(buf_info);

    GlobusGFSFileDebugExitWithError();
    return result;
}

/* called locked */
static
void
globus_l_gfs_file_update_concurrency(
    globus_l_file_monitor_t *           monitor)
{
    globus_result_t                     result;
    int                                 optimal_count;
    int                                 extra;
    GlobusGFSName(globus_l_gfs_file_update_concurrency);
    GlobusGFSFileDebugEnter();
    
    if(!monitor->eof)
    {
        monitor->concurrency_check = monitor->concurrency_check_interval;
        monitor->concurrency_check_interval *= 2;
        if(monitor->concurrency_check_interval > 1024)
        {
            monitor->concurrency_check_interval = 1024;
        }
        
        globus_gridftp_server_get_optimal_concurrency(
            monitor->op, &optimal_count);
        extra = optimal_count - monitor->optimal_count;
            
        monitor->optimal_count = optimal_count;
        while(extra-- > 0)
        {
            globus_byte_t *             buffer;
            
            buffer = globus_memory_pop_node(&monitor->mem);
            result = globus_gridftp_server_register_read(
                monitor->op,
                buffer,
                monitor->block_size,
                globus_l_gfs_file_server_read_cb,
                monitor);
            if(result != GLOBUS_SUCCESS)
            {
                globus_memory_push_node(&monitor->mem, buffer);
                result = GlobusGFSErrorWrapFailed(
                    "globus_gridftp_server_register_read", result);
                goto error_register;
            }
            
            monitor->pending_reads++;
        }
    }
    
    GlobusGFSFileDebugExit();
    return;

error_register:
    GlobusGFSFileDebugExitWithError();
}

static
void
globus_l_gfs_file_server_read_cb(
    globus_gfs_operation_t   op,
    globus_result_t                     result,
    globus_byte_t *                     buffer,
    globus_size_t                       nbytes,
    globus_off_t                        offset,
    globus_bool_t                       eof,
    void *                              user_arg)
{
    globus_l_file_monitor_t *           monitor;
    globus_l_buffer_info_t *            buf_info;
    int                                 rc;
    GlobusGFSName(globus_l_gfs_file_server_read_cb);
    GlobusGFSFileDebugEnter();
    
    monitor = (globus_l_file_monitor_t *) user_arg;
    
    globus_mutex_lock(&monitor->lock);
    {
        monitor->pending_reads--;
        if(result != GLOBUS_SUCCESS && monitor->error == NULL)
        {
            monitor->error = GlobusGFSErrorObjWrapFailed("callback", result);
        }
        if(monitor->error != NULL)
        {
            goto error;
        }
        
        if(eof)
        {
            monitor->eof = GLOBUS_TRUE;
        }
        
        buf_info = (globus_l_buffer_info_t *) 
            globus_malloc(sizeof(globus_l_buffer_info_t));
        if(!buf_info)
        {
            monitor->error = GlobusGFSErrorObjMemory("buf_info");
            goto error_alloc;
        }
        
        /* XXX offset may need some interpretation here depending on type of
         * transfer (may need to remember original start offset)
         */
        buf_info->buffer = buffer;
        buf_info->offset = offset;
        buf_info->length = nbytes;
        monitor->concurrency_check--;
        if(monitor->concurrency_check == 0 && !eof)
        {
            globus_l_gfs_file_update_concurrency(monitor);
        }        
        
        rc = globus_priority_q_enqueue(
            &monitor->queue, buf_info, buf_info);
        if(rc != GLOBUS_SUCCESS)
        {
            monitor->error = GlobusGFSErrorObjGeneric(
                "globus_priority_q_enqueue failed");
            goto error_enqueue;
        }

        result = globus_l_gfs_file_dispatch_write(monitor);
        if(result != GLOBUS_SUCCESS)
        {
            monitor->error = GlobusGFSErrorObjWrapFailed(
                "globus_l_gfs_file_dispatch_write", result);
            goto error_dispatch;
        }

    }
    globus_mutex_unlock(&monitor->lock);
    
    GlobusGFSFileDebugExit();
    return;
    
error_enqueue:
error_dispatch:
    /* can't free buf_info, its in queue */
    globus_free(buf_info);
    
error_alloc:
error:
    globus_memory_push_node(&monitor->mem, buffer);
    if(monitor->pending_reads != 0 || monitor->pending_writes != 0)
    {
        /* there are still outstanding callbacks, wait for them */
        globus_mutex_unlock(&monitor->lock);

        GlobusGFSFileDebugExitWithError();
        return;
    }
    globus_l_gfs_file_close(monitor, globus_error_put(monitor->error));
    globus_mutex_unlock(&monitor->lock);

    GlobusGFSFileDebugExitWithError();
}

static
void
globus_l_gfs_file_open_write_cb(
    globus_xio_handle_t                 handle,
    globus_result_t                     result,
    void *                              user_arg)
{
    globus_l_file_monitor_t *           monitor;
    GlobusGFSName(globus_l_gfs_file_open_write_cb);
    GlobusGFSFileDebugEnter();
    
    monitor = (globus_l_file_monitor_t *) user_arg;
    if(result != GLOBUS_SUCCESS)
    {
        result = GlobusGFSErrorWrapFailed(
            "globus_l_gfs_file_open_write_cb", result);
        monitor->file_handle = NULL;
        goto error_open;
    }

    globus_gridftp_server_begin_transfer(
        monitor->op, GLOBUS_GFS_EVENT_TRANSFER_ABORT, monitor);
    
    globus_mutex_lock(&monitor->lock);
    {
        int                             optimal_count;
        globus_size_t                   block_size;
        
        optimal_count = monitor->optimal_count;
        block_size = monitor->block_size;
        while(optimal_count--)
        {
            globus_byte_t *             buffer;
            
            buffer = globus_memory_pop_node(&monitor->mem);
            result = globus_gridftp_server_register_read(
                monitor->op,
                buffer,
                block_size,
                globus_l_gfs_file_server_read_cb,
                monitor);
            if(result != GLOBUS_SUCCESS)
            {
                globus_memory_push_node(&monitor->mem, buffer);
                result = GlobusGFSErrorWrapFailed(
                    "globus_gridftp_server_register_read", result);
                goto error_register;
            }
            
            monitor->pending_reads++;
        }
    }
    globus_mutex_unlock(&monitor->lock);
    
    GlobusGFSFileDebugExit();
    return;

error_register:
    if(monitor->pending_reads != 0)
    {
        /* there are pending reads, need to wait for them */
        monitor->error = globus_error_get(result);
        globus_mutex_unlock(&monitor->lock);

        GlobusGFSFileDebugExitWithError();
        return;
    }
    globus_mutex_unlock(&monitor->lock);

error_open:
    globus_gridftp_server_finished_transfer(monitor->op, result);
    globus_l_gfs_file_monitor_destroy(monitor);

    GlobusGFSFileDebugExitWithError();
}

static
void
globus_l_gfs_file_open_read_cb(
    globus_xio_handle_t                 handle,
    globus_result_t                     result,
    void *                              user_arg);
    
static
globus_result_t
globus_l_gfs_file_open(
    globus_xio_handle_t *               file_handle,
    const char *                        pathname,
    globus_xio_file_flag_t              open_flags,
    void *                              arg)
{
    globus_l_file_monitor_t *           monitor;
    globus_result_t                     result;
    globus_xio_attr_t                   attr;
    globus_xio_stack_t                  stack;
    char *                              perms;
    int                                 timeout;
    GlobusGFSName(globus_l_gfs_file_open);
    GlobusGFSFileDebugEnter();

    monitor = (globus_l_file_monitor_t *) arg;
    
    result = globus_xio_attr_init(&attr);
    if(result != GLOBUS_SUCCESS)
    {
        result = GlobusGFSErrorWrapFailed("globus_xio_attr_init", result);
        goto error_attr;
    }

#ifdef O_DIRECT
    if(globus_gfs_config_get_bool("direct_io"))
    {
        open_flags |= O_DIRECT;
    }    
#endif

    result = globus_xio_attr_cntl(
        attr,
        globus_l_gfs_file_driver,
        GLOBUS_XIO_FILE_SET_FLAGS,
        open_flags);
    if(result != GLOBUS_SUCCESS)
    {
        result = GlobusGFSErrorWrapFailed("globus_xio_attr_init", result);
        goto error_cntl;
    }

    if(open_flags & GLOBUS_XIO_FILE_CREAT)
    {
        perms = globus_gfs_config_get_string("perms");
        if(perms != NULL)
        {
            int                             p = 0;
            
            p = strtoul(perms, NULL, 8);
            if(p > 0 || 
                (perms[0] == '0' && perms[1] == '\0'))
            {
                result = globus_xio_attr_cntl(
                    attr,
                    globus_l_gfs_file_driver,
                    GLOBUS_XIO_FILE_SET_MODE,
                    p);
                if(result != GLOBUS_SUCCESS)
                {
                    globus_gfs_log_message(
                        GLOBUS_GFS_LOG_WARN,
                        "Failed to set default permissions to: %s\n", perms);
                }
            }
            else
            {
                globus_gfs_log_message(
                    GLOBUS_GFS_LOG_WARN,
                    "Invalid default permissions: %s\n", perms);
            }
        }
    }

    timeout = globus_gfs_config_get_int("file_timeout");
    if(timeout > 0)
    {
        globus_reltime_t                delay;

        GlobusTimeReltimeSet(delay, timeout, 0);
        result = globus_xio_attr_cntl(
            attr,
            NULL,
            GLOBUS_XIO_ATTR_SET_TIMEOUT_ALL,
            globus_l_gfs_file_timeout_cb,
            &delay,
            NULL);
        if(result != GLOBUS_SUCCESS)
        {
            globus_gfs_log_message(
                GLOBUS_GFS_LOG_WARN,
                "Unable to set file access timeout of %d seconds\n", 
                timeout);
        }
    }
    
    
    result = globus_xio_stack_init(&stack, NULL);
    if(result != GLOBUS_SUCCESS)
    {
        result = GlobusGFSErrorWrapFailed("globus_xio_stack_init", result);
        goto error_stack;
    }
    result = globus_l_gfs_file_make_stack(
        monitor->op, arg, attr, stack, monitor->session);
    if(result != GLOBUS_SUCCESS)
    {
        goto error_push;
    }

    result = globus_xio_handle_create(file_handle, stack);
    if(result != GLOBUS_SUCCESS)
    {
        result = GlobusGFSErrorWrapFailed("globus_xio_handle_create", result);
        goto error_create;
    }
    
    result = globus_xio_register_open(
        *file_handle,
        pathname,
        attr,
        (open_flags & GLOBUS_XIO_FILE_CREAT) ? 
            globus_l_gfs_file_open_write_cb : 
            globus_l_gfs_file_open_read_cb,
        arg);
    if(result != GLOBUS_SUCCESS)
    {
        result = GlobusGFSErrorWrapFailed("globus_xio_register_open", result);
        goto error_register;
    }
    
    
    globus_xio_attr_destroy(attr);
    globus_xio_stack_destroy(stack);
    
    GlobusGFSFileDebugExit();
    return GLOBUS_SUCCESS;

error_register:
    globus_xio_register_close(*file_handle, NULL, NULL, NULL);
    *file_handle = NULL;
error_create:
error_push:
    globus_xio_stack_destroy(stack);
    
error_stack:
error_cntl:    
    globus_xio_attr_destroy(attr);
    
error_attr:
    GlobusGFSFileDebugExitWithError();
    return result;
}

static
void
globus_l_gfs_file_recv(
    globus_gfs_operation_t              op,
    globus_gfs_transfer_info_t *        transfer_info,
    void *                              user_arg)
{
    globus_result_t                     result;
    globus_l_file_monitor_t *           monitor;
    int                                 optimal_count;
    globus_size_t                       block_size;
    globus_xio_file_flag_t              open_flags;
    globus_off_t                        offset;
    globus_off_t                        length;
    GlobusGFSName(globus_l_gfs_file_recv);
    GlobusGFSFileDebugEnter();

    globus_gridftp_server_get_optimal_concurrency(op, &optimal_count);
    globus_gridftp_server_get_block_size(op, &block_size);
    globus_assert(optimal_count > 0 && block_size > 0);

    result = globus_l_gfs_file_monitor_init(
        &monitor, block_size, optimal_count);
    if(result != GLOBUS_SUCCESS)
    {
        result = GlobusGFSErrorWrapFailed(
            "globus_l_gfs_file_monitor_init", result);
        goto error_alloc;
    }
    monitor->session = (gfs_l_file_session_t *) user_arg;
    
    globus_gridftp_server_get_write_range(
        op,
        &offset,
        &length);

    result = globus_gridftp_server_get_recv_modification_time(
        op,
        &monitor->utime);
    if(result != GLOBUS_SUCCESS)
    {
        result = GlobusGFSErrorWrapFailed(
            "globus_gridftp_server_get_recv_modification_time", result);
        goto error_alloc;
    }
        
    monitor->op = op;
    monitor->pathname = globus_libc_strdup(transfer_info->pathname);
    
    open_flags = GLOBUS_XIO_FILE_BINARY | 
        GLOBUS_XIO_FILE_CREAT | 
        GLOBUS_XIO_FILE_WRONLY;
    if(transfer_info->truncate)
    {
        open_flags |= GLOBUS_XIO_FILE_TRUNC;
    }
    
    if(transfer_info->expected_checksum)
    {
        monitor->expected_cksm = 
            globus_libc_strdup(transfer_info->expected_checksum);
    }
    if(transfer_info->expected_checksum_alg)
    {
        monitor->expected_cksm_alg = 
            globus_libc_strdup(transfer_info->expected_checksum_alg);
    }
    
    result = globus_l_gfs_file_open(
        &monitor->file_handle, transfer_info->pathname, open_flags, monitor);
    if(result != GLOBUS_SUCCESS)
    {
        result = GlobusGFSErrorWrapFailed("globus_l_gfs_file_open", result);
        goto error_open;
    }
    
    GlobusGFSFileDebugExit();
    return;

error_open:
    globus_l_gfs_file_monitor_destroy(monitor);
    
error_alloc:
    globus_gridftp_server_finished_transfer(op, result);

    GlobusGFSFileDebugExitWithError();
}

/**
 * send calls
 */

static
void
globus_l_gfs_file_read_cb(
    globus_xio_handle_t                 xio_handle, 
    globus_result_t                     result,
    globus_byte_t *                     buffer,
    globus_size_t                       len,
    globus_size_t                       nbytes, 
    globus_xio_data_descriptor_t        data_desc,
    void *                              user_arg);
    
/* called LOCKED */
static
globus_result_t
globus_l_gfs_file_dispatch_read(
    globus_l_file_monitor_t *           monitor)
{
    globus_result_t                     result;
    globus_byte_t *                     buffer;
    globus_size_t                       read_length;
    GlobusGFSName(globus_l_gfs_file_dispatch_read);
    GlobusGFSFileDebugEnter();
    
    if(monitor->first_read && monitor->pending_reads == 0 && 
        !monitor->eof && !globus_list_empty(monitor->buffer_list) &&
        !monitor->aborted)
    {
        globus_gridftp_server_get_read_range(
            monitor->op,
            &monitor->read_offset,
            &monitor->read_length);
        if(monitor->read_length == 0)
        {
            monitor->eof = GLOBUS_TRUE;
        }
        else
        {                                        
            if (monitor->file_offset != monitor->read_offset)
            {
                globus_off_t            seek_tmp;
                seek_tmp = monitor->read_offset;
                
                result = globus_xio_handle_cntl(
                    monitor->file_handle,
                    GLOBUS_XIO_QUERY,
                    GLOBUS_XIO_SEEK,
                    seek_tmp,
                    GLOBUS_XIO_FILE_SEEK_SET);
            
                if(result != GLOBUS_SUCCESS)
                {
                    result = GlobusGFSErrorWrapFailed(
                        "globus_xio_handle_cntl", result);
                    goto error_seek;
                }
                monitor->file_offset = monitor->read_offset;
            }
            
        }
        monitor->first_read = GLOBUS_FALSE;
    }             

    if(monitor->pending_reads == 0 && !monitor->eof && 
        !globus_list_empty(monitor->buffer_list) && !monitor->aborted)
    {
        buffer = globus_list_remove(
            &monitor->buffer_list, monitor->buffer_list);
        globus_assert(buffer);
             
        if(monitor->read_length != -1 && 
            monitor->block_size > monitor->read_length)
        {
            read_length = monitor->read_length;
        }
        else
        {
            read_length = monitor->block_size;
        }
        
        result = globus_xio_register_read(
            monitor->file_handle,
            buffer,
            read_length,
            read_length,
            NULL,
            globus_l_gfs_file_read_cb,
            monitor);
        if(result != GLOBUS_SUCCESS)
        {
            globus_list_insert(&monitor->buffer_list, buffer);
            result = GlobusGFSErrorWrapFailed(
                "globus_xio_register_read", result);
            goto error_register;
        }
        
        monitor->pending_reads++;
    }
    
    GlobusGFSFileDebugExit();
    return GLOBUS_SUCCESS;

error_seek:
error_register:
    GlobusGFSFileDebugExitWithError();
    return result;
}

static
void
globus_l_gfs_file_server_write_cb(
    globus_gfs_operation_t   op,
    globus_result_t                     result,
    globus_byte_t *                     buffer,
    globus_size_t                       nbytes,
    void *                              user_arg)
{
    globus_l_file_monitor_t *           monitor;
    GlobusGFSName(globus_l_gfs_file_server_write_cb);
    GlobusGFSFileDebugEnter();
    
    monitor = (globus_l_file_monitor_t *) user_arg;
    
    globus_mutex_lock(&monitor->lock);
    { 
        monitor->pending_writes--;
        globus_list_insert(&monitor->buffer_list, buffer);

        if(result != GLOBUS_SUCCESS && monitor->error == NULL)
        {
            monitor->error = GlobusGFSErrorObjWrapFailed("callback", result);
        }
        if(monitor->error != NULL)
        {
            goto error;
        }
        
        result = globus_l_gfs_file_dispatch_read(monitor);
        if(result != GLOBUS_SUCCESS)
        {
            monitor->error = GlobusGFSErrorObjWrapFailed(
                "globus_l_gfs_file_dispatch_read", result);
            goto error;
        }
        
        if(monitor->pending_reads == 0 && monitor->pending_writes == 0)
        {
            globus_assert(monitor->eof || monitor->aborted);
            globus_l_gfs_file_close(monitor, GLOBUS_SUCCESS);
            globus_mutex_unlock(&monitor->lock);
        }
        else
        {
            globus_mutex_unlock(&monitor->lock);
        }
    }
    /* already unlocked */
    
    GlobusGFSFileDebugExit();
    return;

error:
    if(monitor->pending_reads != 0 || monitor->pending_writes != 0)
    {
        /* there are still outstanding callbacks, wait for them */
        globus_mutex_unlock(&monitor->lock);
    
        GlobusGFSFileDebugExitWithError();
        return;
    }
    globus_l_gfs_file_close(monitor, globus_error_put(monitor->error));
    globus_mutex_unlock(&monitor->lock);

    GlobusGFSFileDebugExitWithError();
}

static
void
globus_l_gfs_file_read_cb(
    globus_xio_handle_t                 xio_handle, 
    globus_result_t                     result,
    globus_byte_t *                     buffer,
    globus_size_t                       len,
    globus_size_t                       nbytes, 
    globus_xio_data_descriptor_t        data_desc,
    void *                              user_arg)
{
    globus_l_file_monitor_t *           monitor;
    GlobusGFSName(globus_l_gfs_file_read_cb);
    GlobusGFSFileDebugEnter();
    
    monitor = (globus_l_file_monitor_t *) user_arg;
    
    globus_mutex_lock(&monitor->lock);
    {
        monitor->pending_reads--;
        if(result != GLOBUS_SUCCESS && monitor->error == NULL)
        {
            if(globus_xio_error_is_eof(result))
            {
                monitor->eof = GLOBUS_TRUE;
            }
            else
            {
                monitor->error = GlobusGFSErrorObjWrapFailed(
                    "callback", result);
            }
        }
        if(monitor->error != NULL)
        {
            globus_list_insert(&monitor->buffer_list, buffer);
            goto error;
        }
        
        if(nbytes > 0)
        {
            result = globus_gridftp_server_register_write(
                monitor->op,
                buffer,
                nbytes,
                monitor->file_offset,
                -1,
                globus_l_gfs_file_server_write_cb,
                monitor);
            if(result != GLOBUS_SUCCESS)
            {
                globus_list_insert(&monitor->buffer_list, buffer);
                monitor->error = GlobusGFSErrorObjWrapFailed(
                    "globus_gridftp_server_register_write", result);
                goto error;
            }
            
            monitor->pending_writes++;
            monitor->file_offset += nbytes;
            if(monitor->read_length != -1)
            {
                monitor->read_length -= nbytes;
            }
        }
        else
        {
            globus_list_insert(&monitor->buffer_list, buffer);
        }
                    
        if(monitor->read_length == 0)
        {
            monitor->first_read = GLOBUS_TRUE;
        }
        
        result = globus_l_gfs_file_dispatch_read(monitor);
        if(result != GLOBUS_SUCCESS)
        {
            monitor->error = GlobusGFSErrorObjWrapFailed(
                "globus_l_gfs_file_dispatch_read", result);
            goto error;
        }
       
        if(monitor->pending_reads == 0 && monitor->pending_writes == 0)
        {
            globus_assert(monitor->eof || monitor->aborted);
            globus_l_gfs_file_close(monitor, GLOBUS_SUCCESS);
            globus_mutex_unlock(&monitor->lock);
        }
        else
        {
            globus_mutex_unlock(&monitor->lock);
        }
    }
    /* already unlocked */

    GlobusGFSFileDebugExit();
    return;

error:
    globus_assert(monitor->pending_reads == 0);
    if(monitor->pending_writes != 0)
    {
        /* there are still outstanding callbacks, wait for them */
        globus_mutex_unlock(&monitor->lock);
    
        GlobusGFSFileDebugExitWithError();
        return;
    }
    globus_l_gfs_file_close(monitor, globus_error_put(monitor->error));
    globus_mutex_unlock(&monitor->lock);

    GlobusGFSFileDebugExitWithError();
}

static
void
globus_l_gfs_file_open_read_cb(
    globus_xio_handle_t                 handle,
    globus_result_t                     result,
    void *                              user_arg)
{
    globus_l_file_monitor_t *           monitor;
    GlobusGFSName(globus_l_gfs_file_open_read_cb);
    GlobusGFSFileDebugEnter();
    
    monitor = (globus_l_file_monitor_t *) user_arg;
    if(result != GLOBUS_SUCCESS)
    {
        result = GlobusGFSErrorWrapFailed(
            "globus_l_gfs_file_open_write_cb", result);
        monitor->file_handle = NULL;
        goto error_open;
    }
    
    globus_gridftp_server_begin_transfer(
        monitor->op, GLOBUS_GFS_EVENT_TRANSFER_ABORT, monitor);
    
    globus_mutex_lock(&monitor->lock);
    monitor->first_read = GLOBUS_TRUE;
    result = globus_l_gfs_file_dispatch_read(monitor);
    if(result != GLOBUS_SUCCESS)
    {
        monitor->error = GlobusGFSErrorObjWrapFailed(
            "globus_l_gfs_file_dispatch_read", result);
        goto error_dispatch;
    }
    
    if(monitor->pending_reads == 0 && monitor->pending_writes == 0)
    {
        globus_assert(monitor->eof || monitor->aborted);

        globus_l_gfs_file_close(monitor, GLOBUS_SUCCESS);
        globus_mutex_unlock(&monitor->lock);
    }
    else
    {
        globus_mutex_unlock(&monitor->lock);
    }
    
    GlobusGFSFileDebugExit();
    return;

error_dispatch:
    globus_mutex_unlock(&monitor->lock);

error_open:
    globus_gridftp_server_finished_transfer(monitor->op, result);
    globus_l_gfs_file_monitor_destroy(monitor);

    GlobusGFSFileDebugExitWithError();
}

static
void
globus_l_gfs_file_send(
    globus_gfs_operation_t              op,
    globus_gfs_transfer_info_t *        transfer_info,
    void *                              user_arg)
{
    globus_result_t                     result;
    globus_l_file_monitor_t *           monitor;
    int                                 optimal_count;
    globus_size_t                       block_size;
    globus_xio_file_flag_t              open_flags;
    GlobusGFSName(globus_l_gfs_file_send);
    GlobusGFSFileDebugEnter();
    
    globus_gridftp_server_get_optimal_concurrency(op, &optimal_count);
    globus_gridftp_server_get_block_size(op, &block_size);
    globus_assert(optimal_count > 0 && block_size > 0);
    
    result = globus_l_gfs_file_monitor_init(
        &monitor, block_size, optimal_count);
    if(result != GLOBUS_SUCCESS)
    {
        result = GlobusGFSErrorWrapFailed(
            "globus_l_gfs_file_monitor_init", result);
        goto error_alloc;
    }
          
    while(optimal_count--)
    {
        globus_byte_t *                 buffer;
        buffer = globus_memory_pop_node(&monitor->mem);
        globus_list_insert(&monitor->buffer_list, buffer);
    }
    monitor->session = (gfs_l_file_session_t *) user_arg;

    monitor->op = op;
    monitor->pathname = globus_libc_strdup(transfer_info->pathname);

    open_flags = GLOBUS_XIO_FILE_BINARY | GLOBUS_XIO_FILE_RDONLY;

    result = globus_l_gfs_file_open(
        &monitor->file_handle, transfer_info->pathname, open_flags, monitor);
    if(result != GLOBUS_SUCCESS)
    {
        result = GlobusGFSErrorWrapFailed("globus_l_gfs_file_open", result);
        goto error_open;
    }
    
    GlobusGFSFileDebugExit();
    return;

error_open:
    globus_l_gfs_file_monitor_destroy(monitor);
    
error_alloc:  
    globus_gridftp_server_finished_transfer(op, result);

    GlobusGFSFileDebugExitWithError();
}

static
void
globus_l_gfs_file_event(
    globus_gfs_event_info_t *           event_info,
    void *                              user_arg)
{
    globus_l_file_monitor_t *           monitor;
    GlobusGFSName(globus_l_gfs_file_event);
    GlobusGFSFileDebugEnter();
        
    monitor = (globus_l_file_monitor_t *) event_info->event_arg;

    switch(event_info->type)
    {
        case GLOBUS_GFS_EVENT_TRANSFER_ABORT:
            globus_mutex_lock(&monitor->lock);
            {
                monitor->aborted = GLOBUS_TRUE;
            }
            globus_mutex_unlock(&monitor->lock);
            
            globus_xio_handle_cancel_operations(
                monitor->file_handle,
                GLOBUS_XIO_CANCEL_OPEN | 
                GLOBUS_XIO_CANCEL_READ |
                GLOBUS_XIO_CANCEL_WRITE);
            break;
            
        default:
            break;
    }
    
    GlobusGFSFileDebugExit();
}

static
void
globus_l_gfs_file_init(
    globus_gfs_operation_t              op,
    globus_gfs_session_info_t *         session_info)
{
    gfs_l_file_session_t *              session_h;
    GlobusGFSName(globus_l_gfs_file_send);
    GlobusGFSFileDebugEnter();

    session_h = (gfs_l_file_session_t *) globus_calloc(
        1, sizeof(gfs_l_file_session_t));
    session_h->cred = session_info->del_cred;
    session_h->sbj = globus_libc_strdup(session_info->subject);
    session_h->username = globus_libc_strdup(session_info->username);
    session_h->pw = globus_libc_strdup(session_info->password);

    globus_gridftp_server_set_checksum_support(op, GFS_L_FILE_CKSM_SUPPORT);

    /* just make it so we can get the cred. */
    globus_gridftp_server_finished_session_start(
        op,
        GLOBUS_SUCCESS,
        session_h,
        NULL,
        NULL);

    GlobusGFSFileDebugExit();
}

static
void
globus_l_gfs_file_destroy(
    void *                              user_arg)
{
    gfs_l_file_session_t *              session_h;
    session_h = (gfs_l_file_session_t *) user_arg;

    if(session_h)
    {
        if(session_h->sbj != NULL)
        {
            globus_free(session_h->sbj);
        }
        if(session_h->username != NULL)
        {
            globus_free(session_h->username);
        }
        if(session_h->pw != NULL)
        {
            globus_free(session_h->pw);
        }
        globus_free(session_h);
    }
}

static
int
globus_l_gfs_file_activate(void);

static
int
globus_l_gfs_file_deactivate(void);

static globus_gfs_storage_iface_t       globus_l_gfs_file_dsi_iface = 
{
    GLOBUS_GFS_DSI_DESCRIPTOR_SENDER | GLOBUS_GFS_DSI_DESCRIPTOR_HAS_REALPATH,
    globus_l_gfs_file_init,
    globus_l_gfs_file_destroy,
    NULL, /* list */
    globus_l_gfs_file_send,
    globus_l_gfs_file_recv,
    globus_l_gfs_file_event, /* trev */
    NULL, /* active */
    NULL, /* passive */
    NULL, /* data destroy */
    globus_l_gfs_file_command, 
    globus_l_gfs_file_stat,
    NULL,
    NULL,
    globus_l_gfs_file_realpath
};

GlobusExtensionDefineModule(globus_gridftp_server_file) =
{
    "globus_gridftp_server_file",
    globus_l_gfs_file_activate,
    globus_l_gfs_file_deactivate,
    NULL,
    NULL,
    &local_version
};

static
int
globus_l_gfs_file_activate(void)
{
    if(globus_module_activate(GLOBUS_XIO_MODULE) != GLOBUS_SUCCESS)
    {
        goto error_activate;
    }
    
    if(globus_xio_driver_load(
        "file", &globus_l_gfs_file_driver) != GLOBUS_SUCCESS)
    {
        goto error_load_file;
    }
    
    globus_extension_registry_add(
        GLOBUS_GFS_DSI_REGISTRY,
        "file",
        GlobusExtensionMyModule(globus_gridftp_server_file),
        &globus_l_gfs_file_dsi_iface);

    GlobusDebugInit(GLOBUS_GRIDFTP_SERVER_FILE,
        ERROR WARNING TRACE INTERNAL_TRACE INFO STATE INFO_VERBOSE);
    
    return GLOBUS_SUCCESS;
    
error_load_file:
    globus_module_deactivate(GLOBUS_XIO_MODULE);
    
error_activate:
    return GLOBUS_FAILURE;
}

static
int
globus_l_gfs_file_deactivate(void)
{
    globus_extension_registry_remove(
        GLOBUS_GFS_DSI_REGISTRY, "file");
        
    globus_xio_driver_unload(globus_l_gfs_file_driver);
    
    return globus_module_deactivate(GLOBUS_XIO_MODULE);
}
