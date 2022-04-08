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

#include "globus_i_gass_copy.h"

#ifndef TARGET_ARCH_WIN32
#include <fnmatch.h>
#else
#define fnmatch(p,f,l) globus_l_win_fnmatch(p,f)
static
int globus_l_win_fnmatch(
    const char * pattern,
    const char * filename);
#endif

#ifndef MAXPATHLEN
#define MAXPATHLEN 4096
#endif

/************************************************************
 * glob support
 ***********************************************************/


/*    
todo:
come up with minimal globbing support for windows
*/


typedef enum
{
    GLOBUS_GASS_COPY_FTP_OP_NLST,
    GLOBUS_GASS_COPY_FTP_OP_MLSD
} globus_l_gass_copy_ftp_op_t;


typedef struct
{
    globus_mutex_t                     mutex;
    globus_cond_t                      cond;
    globus_object_t *                  err;
    int                                callbacks_left;
    globus_size_t                      buffer_length;
    char *                             url;
    char *                             base_url;
    int                                base_url_len;
    char *                             glob_pattern;
    char *                             list_buffer;
    globus_l_gass_copy_ftp_op_t        list_op;
    globus_gass_copy_handle_t *        handle;
    globus_gass_copy_attr_t *          attr;
    globus_gass_copy_glob_entry_cb_t   entry_cb;
    void *                             entry_user_arg;
    globus_gass_copy_callback_t        op_cb;
    void *                             op_cb_arg;
} globus_l_gass_copy_glob_info_t; 

static
globus_result_t
globus_l_gass_copy_glob_expand_file_url(
    globus_l_gass_copy_glob_info_t *   info);

static
globus_result_t
globus_l_gass_copy_glob_expand_ftp_url(
    globus_l_gass_copy_glob_info_t *   info);

static
globus_result_t
globus_l_gass_copy_glob_parse_ftp_list(
    globus_l_gass_copy_glob_info_t *   info);
    
static
globus_result_t
globus_l_gass_copy_glob_ftp_list(
    globus_l_gass_copy_glob_info_t *   info);
    
static
void
globus_l_gass_copy_ftp_client_op_done_callback(
        void *                         user_arg,
        globus_ftp_client_handle_t *   handle,
        globus_object_t *              err);

static    
void
globus_l_gass_copy_ftp_client_list_read_callback(
    void *                             user_arg,
    globus_ftp_client_handle_t *       handle,
    globus_object_t *                  err,
    globus_byte_t *                    buffer,
    globus_size_t                      length,
    globus_off_t                       offset,
    globus_bool_t                      eof);

static
globus_result_t
globus_l_gass_copy_mkdir_file(
    char *                              url);
    
static
globus_result_t
globus_l_gass_copy_mkdir_ftp(
    globus_gass_copy_handle_t * handle,
    char * url,
    globus_gass_copy_attr_t * attr);

static
void
globus_l_gass_copy_urlencode(
    const char *                        in_string,
    char **                             out_string);


#define GLOBUS_GASS_COPY_FTP_LIST_BUFFER_SIZE 256*1024


globus_result_t 
globus_gass_copy_glob_expand_url( 
     globus_gass_copy_handle_t *        handle, 
     const char *                       url, 
     globus_gass_copy_attr_t *          attr,
     globus_gass_copy_glob_entry_cb_t   entry_cb,
     void *                             user_arg)
{ 
    static char *   myname = "globus_gass_copy_glob_expand_url";
    globus_l_gass_copy_glob_info_t *    info;
    globus_result_t                     result;
    int                                 retval;
    globus_url_scheme_t                 scheme_type;
    int                                 url_len;
    int                                 path_len;
    globus_bool_t                       glob = GLOBUS_TRUE;
    globus_gass_copy_glob_stat_t *      stat_info;  
    char *                              path;


    stat_info = (globus_gass_copy_glob_stat_t *)
         globus_calloc(1, sizeof(globus_gass_copy_glob_stat_t));
         
    info = (globus_l_gass_copy_glob_info_t *)
        globus_malloc(sizeof(globus_l_gass_copy_glob_info_t));
    
    info->handle = handle;
    info->attr = attr;
    info->url = globus_libc_strdup(url);
    info->entry_cb = entry_cb;
    info->entry_user_arg = user_arg;
                

    retval = globus_url_get_scheme(info->url, &scheme_type);

    if(retval)
    {
        result = globus_error_put(
            globus_error_construct_string(
                GLOBUS_GASS_COPY_MODULE,
                GLOBUS_NULL,
                "[%s]: error parsing url scheme.",
                myname));
        goto error;
    }
    
    /* find start of path so we ignore potential globbing chars in host/port */
    url_len = strlen(info->url);
    if(scheme_type != GLOBUS_URL_SCHEME_FILE &&
        (path = strchr(info->url, '/')) &&
        (path = strchr(path + 1, '/')) &&
        (path = strchr(path + 1, '/')))
    {
        path_len = strlen(path);
    }
    else
    {
        path = info->url;
        path_len = url_len;
    }
    
    /* check if url contains glob characters,
       and append * if it is a directory */
       
    if(strcspn(path, "[]*?") == path_len)
    {        
        /* no globbing chars in path */
        if(info->url[url_len - 1] == '/')
        {   
            info->url = (char *) globus_realloc(
                info->url, (url_len + 2) * sizeof(char));
                
            info->url[url_len++] = '*';
            info->url[url_len] = '\0'; 
        }
        else
        if(handle->always_stat_on_expand)
        {
            result = globus_gass_copy_stat(
                handle, info->url, attr, stat_info);
            if(result == GLOBUS_SUCCESS)
            {
                info->entry_cb(
                    info->url,
                    stat_info,
                    info->entry_user_arg);
                    
                if(stat_info->symlink_target)
                {
                    globus_free(stat_info->symlink_target);
                }
                if(stat_info->unique_id)
                {
                    globus_free(stat_info->unique_id);
                }
            }
                
            glob = GLOBUS_FALSE;
        }
        else
        {
            stat_info->type = GLOBUS_GASS_COPY_GLOB_ENTRY_UNKNOWN;
            stat_info->unique_id = GLOBUS_NULL;
            stat_info->symlink_target = GLOBUS_NULL;
            stat_info->mode = -1;
            stat_info->mdtm = -1;
            stat_info->size = -1;
            
            info->entry_cb(
                info->url,
                stat_info,
                info->entry_user_arg);
                 
            glob = GLOBUS_FALSE;
            result = GLOBUS_SUCCESS;
        }
    }

    if(glob)
    {
        switch (scheme_type)
        {
          case GLOBUS_URL_SCHEME_FTP:
          case GLOBUS_URL_SCHEME_GSIFTP:
          case GLOBUS_URL_SCHEME_SSHFTP:
            result = globus_l_gass_copy_glob_expand_ftp_url(info);
            break;
            
          case GLOBUS_URL_SCHEME_FILE:
            result = globus_l_gass_copy_glob_expand_file_url(info);
            break;
              
          default:
            result = globus_error_put(
                globus_error_construct_string(
                    GLOBUS_GASS_COPY_MODULE,
                    GLOBUS_NULL,
                    "[%s]: Globbing not supported with URL scheme.",
                    myname));
            goto error;
            break;    
        }
    }        

    globus_free(info->url);
    globus_free(info);
    globus_free(stat_info);
    return result;
            
error:
    globus_free(info->url);
    globus_free(info);
    globus_free(stat_info);
    return result;
}


static
globus_result_t
globus_l_gass_copy_glob_expand_file_url( 
    globus_l_gass_copy_glob_info_t *    info)
{
    static char *   myname = "globus_l_gass_copy_glob_expand_file_url";
    globus_result_t                     result;
    int                                 retval;
    globus_url_t                        parsed_url;
    int                                 i;
    struct stat                         stat_buf;
    char                                unique_id[256];
    globus_gass_copy_glob_entry_t       type;
    globus_gass_copy_glob_stat_t        info_stat;  
    char                                matched_url[MAXPATHLEN*2];
    char *                              encoded_path;
    struct dirent *                     dir_entry;
    char                                symlink_target[MAXPATHLEN*2];
    DIR *                               dir;

    info->base_url = globus_libc_strdup(info->url);

    info->glob_pattern = strrchr(info->base_url, '/');
    globus_url_string_hex_decode(info->glob_pattern);
    
    if(info->glob_pattern == GLOBUS_NULL || *info->glob_pattern == '\0')
    {
        result = globus_error_put(
            globus_error_construct_string(
                GLOBUS_GASS_COPY_MODULE,
                GLOBUS_NULL,
                "[%s]: Bad URL",
                myname));
        goto error_url;
    }

    *(info->glob_pattern++) = '\0';    
    info->base_url_len = strlen(info->base_url);

    retval = globus_url_parse_loose(info->base_url, &parsed_url);
    if(retval != 0 || parsed_url.url_path == GLOBUS_NULL)
    {
        result = globus_error_put(
            globus_error_construct_string(
                GLOBUS_GASS_COPY_MODULE,
                GLOBUS_NULL,
                "[%s]: error parsing url path: %s",
                myname,
                info->base_url));
        goto error_url;
    }    

    if(stat(parsed_url.url_path, &stat_buf) != 0)
    {
        result = globus_error_put(
            globus_error_construct_string(
                GLOBUS_GASS_COPY_MODULE,
                GLOBUS_NULL,
                "[%s]: unable to access url path: %s",
                myname,
                parsed_url.url_path));
        goto error_stat;
    }
    
    if(!S_ISDIR(stat_buf.st_mode))
    {
        result = globus_error_put(
            globus_error_construct_string(
                GLOBUS_GASS_COPY_MODULE,
                GLOBUS_NULL,
                "[%s]: path is not a dir: %s",
                myname,
                parsed_url.url_path));
        goto error_stat;
    }

    dir = globus_libc_opendir(parsed_url.url_path);
    if(!dir)
    {
        result = globus_error_put(
            globus_error_construct_string(
                GLOBUS_GASS_COPY_MODULE,
                GLOBUS_NULL,
                "[%s]: unable to open url path, %s",
                myname,
                parsed_url.url_path));
        goto error_open;
    }
            
    for(i = 0;
        globus_libc_readdir_r(dir, &dir_entry) == 0 && dir_entry;
        i++)
    {
        char                        path[MAXPATHLEN];
        
        if(dir_entry->d_name[0] == '.' && (dir_entry->d_name[1] == '\0' || 
            (dir_entry->d_name[1] == '.' && dir_entry->d_name[2] == '\0')))
        {
            globus_free(dir_entry);
            continue;
        } 

        if(fnmatch(
            info->glob_pattern,
            dir_entry->d_name,
            0) != 0)
        {
            globus_free(dir_entry);
            continue;
        }

        snprintf(path, sizeof(path), 
            "%s/%s", parsed_url.url_path, dir_entry->d_name);
        path[MAXPATHLEN - 1] = '\0';
#ifndef TARGET_ARCH_WIN32
        if(lstat(path, &stat_buf) != 0)
#else
        if(stat(path, &stat_buf) != 0)
#endif
        {
            result = globus_error_put(
                globus_error_construct_string(
                    GLOBUS_GASS_COPY_MODULE,
                    GLOBUS_NULL,
                    "[%s]: invalid entry in dir: %s",
                    myname,
                    path));
            globus_free(dir_entry);
            continue;
        }

        *symlink_target = '\0';
#ifdef S_ISLNK
        if(S_ISLNK(stat_buf.st_mode))
        {
            if(stat(path, &stat_buf) != 0)
            {
                result = globus_error_put(
                    globus_error_construct_string(
                        GLOBUS_GASS_COPY_MODULE,
                        GLOBUS_NULL,
                        "[%s]: invalid symlink entry in dir: %s",
                        myname,
                        path)); 
                globus_free(dir_entry);
                continue;
            }
            if(realpath(path, symlink_target) == NULL)
            {
                result = globus_error_put(
                    globus_error_construct_string(
                        GLOBUS_GASS_COPY_MODULE,
                        GLOBUS_NULL,
                        "[%s]: unable to find path of symlink in dir: %s",
                        myname,
                        path)); 
                globus_free(dir_entry);
                continue;
            }
        }    
#endif
 
        if(S_ISDIR(stat_buf.st_mode))
        {
            type = GLOBUS_GASS_COPY_GLOB_ENTRY_DIR;
        }
        else
        {                 
            type = GLOBUS_GASS_COPY_GLOB_ENTRY_FILE;
        } 

        *unique_id = '\0';        
        sprintf(
            unique_id,
            "%lx-%lx;",
            (unsigned long) stat_buf.st_dev, 
            (unsigned long) stat_buf.st_ino);

        globus_l_gass_copy_urlencode(dir_entry->d_name, &encoded_path);

        sprintf(
            matched_url, 
            "%s/%s%s", 
            info->base_url, 
            encoded_path,
            type == GLOBUS_GASS_COPY_GLOB_ENTRY_DIR ? "/" : "");

        if(encoded_path)
        {
            globus_free(encoded_path);
            encoded_path = NULL;
        }

        info_stat.type = type;
        info_stat.unique_id = unique_id;
        info_stat.symlink_target = *symlink_target ? symlink_target : NULL;
        info_stat.mode = stat_buf.st_mode & 07777;
        info_stat.mdtm = (int) stat_buf.st_mtime;
        info_stat.size = stat_buf.st_size;
        
        info->entry_cb(
            matched_url,
            &info_stat,
            info->entry_user_arg);

        globus_free(dir_entry);
    }
        
    closedir(dir);
    globus_url_destroy(&parsed_url);
    globus_free(info->base_url);
    
    return GLOBUS_SUCCESS;    
   
error_stat:
error_open:
    globus_url_destroy(&parsed_url);

error_url:
    globus_free(info->base_url);

    return result;
}    


static
globus_result_t
globus_l_gass_copy_glob_expand_ftp_url(
    globus_l_gass_copy_glob_info_t *    info)
{
    static char *   myname = "globus_l_gass_copy_glob_expand_ftp_url";    
    globus_result_t                    result;    
    globus_ftp_client_tristate_t       feature_response;
    globus_ftp_client_features_t       features;
    char *                             tmp;
    


    info->base_url = globus_libc_strdup(info->url);
    tmp = strrchr(info->base_url, '/');
    if(tmp == GLOBUS_NULL || *tmp == '\0')
    {
        result = globus_error_put(
            globus_error_construct_string(
                GLOBUS_GASS_COPY_MODULE,
                GLOBUS_NULL,
                "[%s]: Bad URL",
                myname));
        goto error_url;
    }

    tmp++;
    info->glob_pattern = globus_libc_strdup(tmp);
    *tmp = '\0';

    globus_url_string_hex_decode(info->glob_pattern);
   
    info->base_url_len = strlen(info->base_url);
    info->list_buffer = GLOBUS_NULL;
    info->buffer_length = 0;
    info->err = GLOBUS_NULL;
            
    globus_mutex_init(&info->mutex, GLOBUS_NULL);
    globus_cond_init(&info->cond, GLOBUS_NULL);

    result = globus_ftp_client_features_init(&features);

    if(result != GLOBUS_SUCCESS)
    {
        goto error_feat_init;
    }

    info->callbacks_left = 1;
    result = globus_ftp_client_feat(
        &info->handle->ftp_handle,
        info->base_url,
        info->attr->ftp_attr,
        &features,
        globus_l_gass_copy_ftp_client_op_done_callback,
        info);
     
    if(result != GLOBUS_SUCCESS)
    {
        goto error_feat;
    }
                 
    globus_mutex_lock(&info->mutex);
    while(info->callbacks_left)
    {
        globus_cond_wait(&info->cond, &info->mutex);
    }
    if(info->err)
    {
        result = globus_error_put(info->err);
        info->err = GLOBUS_NULL;
    }
    globus_mutex_unlock(&info->mutex);
  
    if(result != GLOBUS_SUCCESS)
    {
        goto error_feat;
    }

    result = globus_ftp_client_is_feature_supported(
                &features, 
                &feature_response, 
                GLOBUS_FTP_CLIENT_FEATURE_MLST);
                    
    if(result != GLOBUS_SUCCESS)
    {
        goto error_feat;
    }
    
    if(feature_response == GLOBUS_FTP_CLIENT_TRUE)
    {       
        info->list_op = GLOBUS_GASS_COPY_FTP_OP_MLSD;
    }
    else    
    {
        info->list_op = GLOBUS_GASS_COPY_FTP_OP_NLST;
    }

    result = globus_l_gass_copy_glob_ftp_list(info);    
         
    if(result != GLOBUS_SUCCESS)
    {
        goto error_list;
    }

    result = globus_l_gass_copy_glob_parse_ftp_list(info);   
    
    if(result != GLOBUS_SUCCESS)
    {
        goto error_list;
    } 

    if(info->list_buffer != GLOBUS_NULL)
    {        
        globus_free(info->list_buffer);
    }
   
    globus_ftp_client_features_destroy(&features);

    globus_cond_destroy(&info->cond);
    globus_mutex_destroy(&info->mutex);
        
    globus_free(info->base_url);
    globus_free(info->glob_pattern);

    return GLOBUS_SUCCESS;


error_list:
error_feat:
    globus_ftp_client_features_destroy(&features);

error_feat_init:    
    globus_cond_destroy(&info->cond);
    globus_mutex_destroy(&info->mutex);
    globus_free(info->glob_pattern);

error_url:
    globus_free(info->base_url);

    return result;
}

static
globus_result_t
globus_l_gass_copy_glob_ftp_list(
    globus_l_gass_copy_glob_info_t *    info)
{
    static char *   myname = "globus_l_gass_copy_glob_ftp_list";    

    globus_result_t                     result;
    globus_byte_t *                     read_buffer;
    
    read_buffer = (globus_byte_t *)
        globus_malloc(GLOBUS_GASS_COPY_FTP_LIST_BUFFER_SIZE * 
            sizeof(globus_byte_t));

    if(read_buffer == GLOBUS_NULL)
    {
        result = globus_error_put(
            globus_error_construct_string(
            GLOBUS_GASS_COPY_MODULE,
            GLOBUS_NULL,
            "[%s]: Memory allocation error",
            myname));
        goto error_malloc;
    }        

    
    info->callbacks_left = 2;        
    if(info->list_op == GLOBUS_GASS_COPY_FTP_OP_MLSD)
    {
        result = globus_ftp_client_machine_list(
                     &info->handle->ftp_handle,
                     info->base_url,
                     info->attr->ftp_attr,
                     globus_l_gass_copy_ftp_client_op_done_callback,
                     info);
    }
    else    
    {
        result = globus_ftp_client_list(
                     &info->handle->ftp_handle,
                     info->base_url,
                     info->attr->ftp_attr,
                     globus_l_gass_copy_ftp_client_op_done_callback,
                     info);
    }

    if(result != GLOBUS_SUCCESS)
    {
        goto error_list;
    }

    result = globus_ftp_client_register_read(
                 &info->handle->ftp_handle,
                 read_buffer,
                 GLOBUS_GASS_COPY_FTP_LIST_BUFFER_SIZE,
                 globus_l_gass_copy_ftp_client_list_read_callback,
                 info);

    if(result != GLOBUS_SUCCESS)
    {
        globus_ftp_client_abort(&info->handle->ftp_handle);
        
        globus_mutex_lock(&info->mutex);
        info->callbacks_left--;
        while(info->callbacks_left > 0)
        {
            globus_cond_wait(&info->cond, &info->mutex);
        }
        globus_mutex_unlock(&info->mutex);
        
        goto error_read;    
    }

    globus_mutex_lock(&info->mutex);
    while(info->callbacks_left)
    {
        globus_cond_wait(&info->cond, &info->mutex);
    }
    globus_mutex_unlock(&info->mutex);

    if(info->err)
    {
        result = globus_error_put(info->err);
        info->err = GLOBUS_NULL;
    }

    if(result != GLOBUS_SUCCESS)
    {
        goto error_read;
    }

    if(read_buffer != (globus_byte_t *) info->list_buffer)
    {        
        globus_free(read_buffer);
    }



    return GLOBUS_SUCCESS;


error_read:
error_list:

    if(read_buffer != GLOBUS_NULL)
    {        
        globus_free(read_buffer);
    }
    if(info->err)
    {
        globus_object_free(info->err);
        info->err = GLOBUS_NULL;
    }

error_malloc:
    return result;
}


static
void
globus_l_gass_copy_ftp_client_op_done_callback(
    void *                             user_arg,
    globus_ftp_client_handle_t *       handle,
    globus_object_t *                  err)
{
    globus_l_gass_copy_glob_info_t * info;

    info = (globus_l_gass_copy_glob_info_t *) user_arg;
           
    globus_mutex_lock(&info->mutex);
    if (err && !info->err)
    {
        info->err = globus_object_copy(err);
    }
    info->callbacks_left--;
    globus_cond_signal(&info->cond);
    globus_mutex_unlock(&info->mutex);
    
    return;
}

static
void
globus_l_gass_copy_ftp_client_cksm_done_callback(
    void *                             user_arg,
    globus_ftp_client_handle_t *       handle,
    globus_object_t *                  err)
{
    globus_l_gass_copy_glob_info_t * info;

    info = (globus_l_gass_copy_glob_info_t *) user_arg;
    if(info->op_cb)
    {
       info->op_cb(info->op_cb_arg, info->handle, err); 
    }
    else
    {
        globus_mutex_lock(&info->mutex);
        if(err && !info->err)
        {
            info->err = globus_object_copy(err);
        }
        info->callbacks_left--;
        globus_cond_signal(&info->cond);
        globus_mutex_unlock(&info->mutex);
    }
    
    globus_free(info);
    
    return;
}

static
int
globus_l_gass_copy_mdtm_to_timet(
    char *                              mdtm_str,
    int *                               time_out)
{
    char *			        p;
    struct tm                           tm;
    struct tm                           gmt_now_tm;
    struct tm *                         gmt_now_tm_p;
    time_t                              offset;
    time_t                              gmt_now;
    time_t                              now;
    time_t                              file_time;
    int 				rc;

    p = mdtm_str;
    
    memset(&tm, '\0', sizeof(struct tm));

    /* 4 digit year */
    rc = sscanf(p, "%04d", &tm.tm_year);
    if(rc != 1)
    {
	goto error_exit;
    }
    tm.tm_year -= 1900;
    p += 4;

    /* 2 digit month [01-12] */
    rc = sscanf(p, "%02d", &tm.tm_mon);
    if(rc != 1)
    {
	goto error_exit;
    }
    tm.tm_mon--;
    p += 2;

    /* 2 digit day/month [01-31] */
    rc = sscanf(p, "%02d", &tm.tm_mday);
    if(rc != 1)
    {
	goto error_exit;
    }
    p += 2;

    /* 2 digit hour [00-23] */
    rc = sscanf(p, "%02d", &tm.tm_hour);
    if(rc != 1)
    {
	goto error_exit;
    }
    p += 2;

    /* 2 digit minute [00-59] */
    rc = sscanf(p, "%02d", &tm.tm_min);
    if(rc != 1)
    {
	goto error_exit;
    }
    p += 2;

    /* 2 digit second [00-60] */
    rc = sscanf(p, "%02d", &tm.tm_sec);
    if(rc != 1)
    {
	goto error_exit;
    }
    p += 2;
    
    file_time = mktime(&tm);
    if(file_time == (time_t) -1)
    {
	goto error_exit;
    }
    
    now = time(&now);
    if(now == (time_t) -1)
    {
	goto error_exit;
    }
    
    memset(&gmt_now_tm, '\0', sizeof(struct tm));
    gmt_now_tm_p = globus_libc_gmtime_r(&now, &gmt_now_tm);
    if(gmt_now_tm_p == NULL)
    {
	goto error_exit;
    }
    
    gmt_now = mktime(&gmt_now_tm);
    if(gmt_now == (time_t) -1)
    {
	goto error_exit;
    }
    
    offset = now - gmt_now;

    *time_out = file_time + offset;

    return GLOBUS_SUCCESS;

error_exit:

    return GLOBUS_FAILURE;
}
    
static
globus_result_t
globus_l_gass_copy_glob_parse_mlst_line(
    char *                              line,
    globus_gass_copy_glob_stat_t *      stat_info)
{
    static char *   myname = "globus_l_gass_copy_glob_parse_ftp_list";
    globus_result_t                     result;
    int                                 i;
    char *                              space;
    char *                              startline;
    char *                              startfact;
    char *                              endfact;
    char *                              factval;
    
    char *                              unique_id = NULL;
    char *                              mode_s = NULL;
    char *                              symlink_target = NULL;
    char *                              modify_s = NULL;
    char *                              size_s = NULL;
    globus_gass_copy_glob_entry_t       type = GLOBUS_GASS_COPY_GLOB_ENTRY_UNKNOWN;

    startline = line;
    
    space = strchr(startline, ' ');
    if(space == GLOBUS_NULL)
    {
        result = globus_error_put(
            globus_error_construct_string(
                GLOBUS_GASS_COPY_MODULE,
                GLOBUS_NULL,
                "[%s]: Bad MLSD response",
                myname));
                  
        goto error_invalid_mlsd;
    }
    *space = '\0';            
    startfact = startline;
    
    while(startfact != space)
    {
        endfact = strchr(startfact, ';');
        if(endfact)
        {             
            *endfact = '\0';
        }
        else
        {                                     
/*
         older MLST-draft spec says ending fact can be missing
         the final semicolon... not a problem to support this,
         no need to die.  (ncftpd does this) 
            
            result = globus_error_put(
                globus_error_construct_string(
                    GLOBUS_GASS_COPY_MODULE,
                    GLOBUS_NULL,
                    "[%s]: Bad MLSD response",
                    myname));
                  
            goto error_invalid_mlsd;
*/

            endfact = space - 1;   
        }
        
        factval = strchr(startfact, '=');
        if(!factval)
        {             
            result = globus_error_put(
                globus_error_construct_string(
                    GLOBUS_GASS_COPY_MODULE,
                    GLOBUS_NULL,
                    "[%s]: Bad MLSD response",
                    myname));
                  
            goto error_invalid_mlsd;
        }
        *(factval++) = '\0';

        for(i = 0; startfact[i] != '\0'; i++)
        {
            startfact[i] = tolower(startfact[i]);
        }
    
        if(strcmp(startfact, "type") == 0)
        {
            if(strcasecmp(factval, "dir") == 0)
            {
                type = GLOBUS_GASS_COPY_GLOB_ENTRY_DIR;
            }
            else if(strcasecmp(factval, "file") == 0)
            {
                type = GLOBUS_GASS_COPY_GLOB_ENTRY_FILE;
            }
            else
            {
                type = GLOBUS_GASS_COPY_GLOB_ENTRY_OTHER;
            } 
        }                 
        if(strcmp(startfact, "unique") == 0)
        {
            unique_id = factval;                        
        }                 
        if(strcmp(startfact, "unix.mode") == 0)
        {
            mode_s = factval;                        
        }
        if(strcmp(startfact, "modify") == 0)
        {
            modify_s = factval;                        
        }                 
        if(strcmp(startfact, "size") == 0)
        {
            size_s = factval;                        
        }                 
        if(strcmp(startfact, "unix.slink") == 0)
        {
            symlink_target = factval;                        
        }                 
        
        startfact = endfact + 1;                                 
    } 

    
    stat_info->type = type;
    stat_info->unique_id = globus_libc_strdup(unique_id);
    stat_info->symlink_target = globus_libc_strdup(symlink_target);
    stat_info->mode = -1;
    stat_info->size = -1;
    stat_info->mdtm = -1;
        
    if(mode_s)
    {
        stat_info->mode = strtoul(mode_s, NULL, 0);
    }
    
    if(size_s)
    {
        globus_off_t            size;
        int                     rc;
        
        rc = sscanf(size_s, "%"GLOBUS_OFF_T_FORMAT, &size);
        if(rc == 1)
        {
            stat_info->size = size;
        }
    }
    
    if(modify_s)
    {
        int                     mdtm;
        
        if(globus_l_gass_copy_mdtm_to_timet(modify_s, &mdtm) == 
            GLOBUS_SUCCESS)
        {
            stat_info->mdtm = mdtm;
        }
    }

    return GLOBUS_SUCCESS;

error_invalid_mlsd:

    return result;
}

static
globus_result_t
globus_l_gass_copy_glob_parse_ftp_list(
    globus_l_gass_copy_glob_info_t *    info)
{
    static char *   myname = "globus_l_gass_copy_glob_parse_ftp_list";
    globus_result_t                     result;
    int                                 i;
    char *                              space;
    char *                              temp_p;
    char *                              filename;
    char *                              startline;
    char *                              endline;
    char *                              startfact;
    char *                              endfact;
    char *                              factval;
    
    char                                matched_url[4096];
    char *                              encoded_path = NULL;
    char *                              unique_id;
    char *                              mode_s;
    char *                              symlink_target;
    char *                              modify_s;
    char *                              size_s;
    globus_gass_copy_glob_entry_t       type;
    globus_gass_copy_glob_stat_t        info_stat;  
       
    startline = info->list_buffer;
    
    while(startline < (info->list_buffer + info->buffer_length))
    {
        type = GLOBUS_GASS_COPY_GLOB_ENTRY_UNKNOWN;
        unique_id = GLOBUS_NULL;
        mode_s = GLOBUS_NULL;
        symlink_target = GLOBUS_NULL;
        size_s = GLOBUS_NULL;
        modify_s = GLOBUS_NULL;
        
        while(*startline == '\r' || 
              *startline == '\n')
        {
            startline++;
            
        }
    
        if(info->list_op == GLOBUS_GASS_COPY_FTP_OP_NLST)
        {
            endline = startline;
            while(endline < info->list_buffer + info->buffer_length &&
                *endline != '\r' && *endline != '\n') 
            {
                endline++;
            } 
            *endline = '\0';
            filename = startline;
        }
        else 
        {
            endline = startline;
            while(endline + 1 < info->list_buffer + info->buffer_length && 
                (*endline != '\r' || *(endline + 1) != '\n'))
            {
                endline++;
            } 
            *endline = '\0';

            space = strchr(startline, ' ');
            if (space == GLOBUS_NULL)
            {
                result = globus_error_put(
                    globus_error_construct_string(
                        GLOBUS_GASS_COPY_MODULE,
                        GLOBUS_NULL,
                        "[%s]: Bad MLSD response",
                        myname));
                          
                goto error_invalid_mlsd;
            }
            *space = '\0';            
            filename = space + 1;
            startfact = startline;
            
            while(startfact != space)
            {
                endfact = strchr(startfact, ';');
                if(endfact)
                {             
                    *endfact = '\0';
                }
                else
                {                                     
/*
                 older MLST-draft spec says ending fact can be missing
                 the final semicolon... not a problem to support this,
                 no need to die.  (ncftpd does this) 
                    
                    result = globus_error_put(
                        globus_error_construct_string(
                            GLOBUS_GASS_COPY_MODULE,
                            GLOBUS_NULL,
                            "[%s]: Bad MLSD response",
                            myname));
                          
                    goto error_invalid_mlsd;
*/

                    endfact = space - 1;   
                }
                
                factval = strchr(startfact, '=');
                if(!factval)
                {             
                    result = globus_error_put(
                        globus_error_construct_string(
                            GLOBUS_GASS_COPY_MODULE,
                            GLOBUS_NULL,
                            "[%s]: Bad MLSD response",
                            myname));
                          
                    goto error_invalid_mlsd;
                }
                *(factval++) = '\0';

                for(i = 0; startfact[i] != '\0'; i++)
                {
                    startfact[i] = tolower(startfact[i]);
                }
            
                if(strcmp(startfact, "type") == 0)
                {
                    if(strcasecmp(factval, "dir") == 0)
                    {
                        type = GLOBUS_GASS_COPY_GLOB_ENTRY_DIR;
                    }
                    else if(strcasecmp(factval, "file") == 0)
                    {
                        type = GLOBUS_GASS_COPY_GLOB_ENTRY_FILE;
                    }
                    else
                    {
                        type = GLOBUS_GASS_COPY_GLOB_ENTRY_OTHER;
                    } 
                }                 
                if(strcmp(startfact, "unique") == 0)
                {
                    unique_id = factval;                        
                }                 
                if(strcmp(startfact, "unix.mode") == 0)
                {
                    mode_s = factval;                        
                }
                if(strcmp(startfact, "modify") == 0)
                {
                    modify_s = factval;                        
                }                 
                if(strcmp(startfact, "size") == 0)
                {
                    size_s = factval;                        
                }                 
                if(strcmp(startfact, "unix.slink") == 0)
                {
                    symlink_target = factval;                        
                }                 
                
                startfact = endfact + 1;                                 
            } 
        }

        temp_p = strrchr(filename, '/');
        if (temp_p != GLOBUS_NULL)
        {
            filename = temp_p + 1;
        }
        
        *matched_url = '\0'; 
        
        globus_l_gass_copy_urlencode(filename, &encoded_path);
        
        if(fnmatch(
               info->glob_pattern,
               filename,
               0) == 0)
        {
            sprintf(
                matched_url, 
                "%s%s%s",
                info->base_url, 
                encoded_path,
                type == GLOBUS_GASS_COPY_GLOB_ENTRY_DIR ? "/" : "");
        }
   
        if(encoded_path)
        {
            globus_free(encoded_path);
            encoded_path = NULL;
        }
    
        if(*matched_url &&
            (type == GLOBUS_GASS_COPY_GLOB_ENTRY_DIR || 
            type == GLOBUS_GASS_COPY_GLOB_ENTRY_FILE ||
            type == GLOBUS_GASS_COPY_GLOB_ENTRY_UNKNOWN) &&
            !(filename[0] == '.' && (filename[1] == '\0' || 
            (filename[1] == '.' && filename[2] == '\0'))) )
        {
            info_stat.type = type;
            info_stat.unique_id = unique_id;
            info_stat.symlink_target = symlink_target;
            info_stat.mode = -1;
            info_stat.size = -1;
            info_stat.mdtm = -1;
                
            if(mode_s)
            {
                info_stat.mode = strtoul(mode_s, NULL, 0);
            }
            
            if(size_s)
            {
                globus_off_t            size;
                int                     rc;
                
                rc = sscanf(size_s, "%"GLOBUS_OFF_T_FORMAT, &size);
                if(rc == 1)
                {
                    info_stat.size = size;
                }
            }
            
            if(modify_s)
            {
                int                     mdtm;
                
                if(globus_l_gass_copy_mdtm_to_timet(modify_s, &mdtm) == 
                    GLOBUS_SUCCESS)
                {
                    info_stat.mdtm = mdtm;
                }
            }
                        
            info->entry_cb(
                matched_url,
                &info_stat,
                info->entry_user_arg);
        }
                
        startline = endline + 1;
        while(startline < info->list_buffer + info->buffer_length && 
            (*startline == '\r' || *startline == '\n'))
        {
            startline++;           
        }

    }
    
    return GLOBUS_SUCCESS;
    
error_invalid_mlsd:

    return result;    
    
}



static
void
globus_l_gass_copy_ftp_client_list_read_callback(
    void *                             user_arg,
    globus_ftp_client_handle_t *       handle,
    globus_object_t *                  err,
    globus_byte_t *                    buffer,
    globus_size_t                      length,
    globus_off_t                       offset,
    globus_bool_t                      eof)
{
    static char *   myname = "globus_l_gass_copy_ftp_client_list_read_callback";    
    globus_l_gass_copy_glob_info_t *   info;
    globus_result_t                    result;
    char *                             temp_p = NULL;
    
    info = (globus_l_gass_copy_glob_info_t *) user_arg;

    if(err)
    {
        goto error_before_callback;
    }

    if(info->list_buffer == GLOBUS_NULL && eof && offset == 0)
    {
        info->list_buffer = (char *) buffer;
        info->buffer_length = length;
        buffer = GLOBUS_NULL;
    }
    else
    {
        if((length + offset) > info->buffer_length)
        {
            temp_p = (char *) 
                globus_realloc(info->list_buffer, length + offset);
            if(temp_p == GLOBUS_NULL)
            {
                result = globus_error_put(
                    globus_error_construct_string(
                        GLOBUS_GASS_COPY_MODULE,
                        GLOBUS_NULL,
                        "[%s]: Memory allocation error",
                        myname));
                goto error_malloc;
            }
    
            info->list_buffer = temp_p;
            info->buffer_length = length + offset;
        }       

        memcpy(
            info->list_buffer + offset,
            buffer, 
            length);
    }
    
    if(!eof)
    {
        result = globus_ftp_client_register_read(
                    handle,
                    buffer,
                    GLOBUS_GASS_COPY_FTP_LIST_BUFFER_SIZE,
                    globus_l_gass_copy_ftp_client_list_read_callback,
                    (void *) info);

        if(result != GLOBUS_SUCCESS)
        {
           goto error_register_read;
        } 
    }
    else
    {
        
        globus_mutex_lock(&info->mutex);
        info->callbacks_left--;
        globus_cond_signal(&info->cond);
        globus_mutex_unlock(&info->mutex);


    }        

    return;


error_register_read:
error_malloc:
error_before_callback:

    globus_mutex_lock(&info->mutex);
    if (err && !info->err)
    {
        info->err = globus_object_copy(err);
    }
    info->callbacks_left--;
    globus_cond_signal(&info->cond);
    globus_mutex_unlock(&info->mutex);

    return;
}


static const char *hex_chars = "0123456789ABCDEF";
#define ALLOWED_CHARS "$-_.+!'\"(),/:@=&"

#define NEEDS_ENCODING(c) \
    (!isalnum(c) && strchr(ALLOWED_CHARS, (c)) == NULL)

/** Encode string using URL encoding from rfc1738 (sec 2.2).
    used to encode paths in resolved urls */
static
void
globus_l_gass_copy_urlencode(
    const char *                        in_string,
    char **                             out_string)
{
    int                                 len;
    char *                              in_ptr;
    char *                              out_ptr;
    char                                out_buf[MAXPATHLEN * 3];

    in_ptr = (char *) in_string;
    out_ptr = out_buf;
    len = strlen(in_string);

    while(in_ptr < in_string + len)
    {
        if(NEEDS_ENCODING(*in_ptr))
        {
            *out_ptr++ = '%';
            *out_ptr++ = hex_chars[(*in_ptr >> 4) & 0xF];
            *out_ptr++ = hex_chars[*in_ptr & 0xF];
        } 
        else
        {
            *out_ptr++ = *in_ptr;
        }
        in_ptr++;
    }
    *out_ptr = '\0';
    *out_string = globus_libc_strdup(out_buf);
}



static
globus_result_t
globus_l_gass_copy_stat_ftp(
    globus_gass_copy_handle_t *         handle,
    char *                              url,
    globus_gass_copy_attr_t *           attr,
    globus_gass_copy_glob_stat_t *      stat_info)
{
    globus_result_t                     result;
    globus_l_gass_copy_glob_info_t      info;
    globus_byte_t *                     mlst_buf;
    globus_size_t                       mlst_len;
    
    info.callbacks_left = 1;
    info.err = GLOBUS_NULL;
    globus_cond_init(&info.cond, GLOBUS_NULL);
    globus_mutex_init(&info.mutex, GLOBUS_NULL);
        
    result = globus_ftp_client_mlst(
        &handle->ftp_handle,
        url,
        attr->ftp_attr,
        &mlst_buf,
        &mlst_len,
        globus_l_gass_copy_ftp_client_op_done_callback,
        &info);
    if(result != GLOBUS_SUCCESS)
    {
        goto error_mlst;
    }

    globus_mutex_lock(&info.mutex);
    while(info.callbacks_left > 0)
    {
        globus_cond_wait(&info.cond, &info.mutex);
    }
    globus_mutex_unlock(&info.mutex);

    if(info.err)
    {
        result = globus_error_put(info.err);
        info.err = GLOBUS_NULL;
    }
    
    if(result != GLOBUS_SUCCESS)
    {
        goto error_mlst;
    }
    
    result = globus_l_gass_copy_glob_parse_mlst_line(
        (char *) mlst_buf, stat_info);
    
    if(result != GLOBUS_SUCCESS)
    {
        goto error_parse;
    }

    globus_free(mlst_buf);
    globus_cond_destroy(&info.cond);
    globus_mutex_destroy(&info.mutex);

    return GLOBUS_SUCCESS;

error_parse:
    globus_free(mlst_buf);

error_mlst:
    globus_cond_destroy(&info.cond);
    globus_mutex_destroy(&info.mutex);

    return result;
}

static
globus_result_t
globus_l_gass_copy_stat_file(
    char *                              url,
    globus_gass_copy_glob_stat_t *      stat_info)
{
    static char *   myname = "globus_l_gass_copy_stat_file";    
    int                                 rc;
    globus_result_t                     result;
    struct stat                         stat_buf;
    globus_url_t                        parsed_url;
    char                                unique_id[256];
    globus_gass_copy_glob_entry_t       type;
    char                                symlink_target[MAXPATHLEN*2];
    
    rc = globus_url_parse(url, &parsed_url);
    
    if(rc != 0)
    {
        result = globus_error_put(
            globus_error_construct_string(
                GLOBUS_GASS_COPY_MODULE,
                GLOBUS_NULL,
                "[%s]: error parsing url: "
                "globus_url_parse returned %d",
                myname,
                rc));
        goto error_url;
    }
    
    if(parsed_url.url_path == GLOBUS_NULL)
    {
        result = globus_error_put(
            globus_error_construct_string(
                GLOBUS_GASS_COPY_MODULE,
                GLOBUS_NULL,
                "[%s]: error parsing url: "
                "url has no path",
                myname));
        goto error_null_path;
    }
    
#ifndef TARGET_ARCH_WIN32
    if(lstat(parsed_url.url_path, &stat_buf) != 0)
#else
    if(stat(parsed_url.url_path, &stat_buf) != 0)
#endif
    {
        result = globus_error_put(
            globus_error_construct_string(
                GLOBUS_GASS_COPY_MODULE,
                GLOBUS_NULL,
                "[%s]: file not found in dir: %s",
                myname,
                parsed_url.url_path));
        goto error_stat;
    }

    *symlink_target = '\0';
#ifdef S_ISLNK
    if(S_ISLNK(stat_buf.st_mode))
    {
        if(stat(parsed_url.url_path, &stat_buf) != 0)
        {
            result = globus_error_put(
                globus_error_construct_string(
                    GLOBUS_GASS_COPY_MODULE,
                    GLOBUS_NULL,
                    "[%s]: invalid symlink entry in dir: %s",
                    myname,
                    parsed_url.url_path)); 
            goto error_stat;
        }
        if(realpath(parsed_url.url_path, symlink_target) == NULL)
        {
            result = globus_error_put(
                globus_error_construct_string(
                    GLOBUS_GASS_COPY_MODULE,
                    GLOBUS_NULL,
                    "[%s]: unable to find path of symlink in dir: %s",
                    myname,
                    parsed_url.url_path));
            goto error_stat;
        }
    }    
#endif

    if(S_ISDIR(stat_buf.st_mode))
    {
        type = GLOBUS_GASS_COPY_GLOB_ENTRY_DIR;
    }
    else
    {                 
        type = GLOBUS_GASS_COPY_GLOB_ENTRY_FILE;
    } 

    *unique_id = '\0';        
    sprintf(
        unique_id,
        "%lx-%lx;",
        (unsigned long) stat_buf.st_dev, 
        (unsigned long) stat_buf.st_ino);

    stat_info->type = type;
    stat_info->unique_id = globus_libc_strdup(unique_id);
    stat_info->symlink_target = *symlink_target ? 
        globus_libc_strdup(symlink_target) : NULL;
    stat_info->mode = stat_buf.st_mode & 07777;
    stat_info->mdtm = (int) stat_buf.st_mtime;
    stat_info->size = stat_buf.st_size;
           
    globus_url_destroy(&parsed_url); 
    return GLOBUS_SUCCESS;

error_stat:    
error_null_path:
    globus_url_destroy(&parsed_url);
    
error_url:

    return result;
    
}

globus_result_t
globus_gass_copy_stat(
    globus_gass_copy_handle_t *         handle,
    char *                              url,
    globus_gass_copy_attr_t *           attr,
    globus_gass_copy_glob_stat_t *      stat_info)
{   
    static char *   myname = "globus_gass_copy_stat";    

    globus_result_t                     result;
    globus_gass_copy_url_mode_t         url_mode;
    
    result = globus_gass_copy_get_url_mode(url, &url_mode);
    if(result != GLOBUS_SUCCESS)
    {
        goto error_exit;
    }
    
    if(url_mode == GLOBUS_GASS_COPY_URL_MODE_FTP)
    {
        result = globus_l_gass_copy_stat_ftp(handle, url, attr, stat_info);

        if(result != GLOBUS_SUCCESS)
        {
            goto error_exit;
        }
    }
    else if(url_mode == GLOBUS_GASS_COPY_URL_MODE_IO)
    {
        result = globus_l_gass_copy_stat_file(url, stat_info);
        if(result != GLOBUS_SUCCESS)
        {
            goto error_exit;
        }
    }
    else
    {
        result = globus_error_put(
            globus_error_construct_string(
                GLOBUS_GASS_COPY_MODULE,
                GLOBUS_NULL,
                "[%s]: unsupported URL scheme: %s",
                myname,
                url));
        goto error_exit;
    }
    
    return GLOBUS_SUCCESS;
    
error_exit:

    return result;
    
}


globus_result_t
globus_gass_copy_mkdir(
    globus_gass_copy_handle_t *         handle,
    char *                              url,
    globus_gass_copy_attr_t *           attr)
{   
    static char *   myname = "globus_gass_copy_mkdir";    

    globus_result_t                     result;
    globus_gass_copy_url_mode_t         url_mode;
    
    result = globus_gass_copy_get_url_mode(url, &url_mode);
    if(result != GLOBUS_SUCCESS)
    {
        goto error_exit;
    }
    
    if(url_mode == GLOBUS_GASS_COPY_URL_MODE_FTP)
    {
        result = globus_l_gass_copy_mkdir_ftp(handle, url, attr);

        if(result != GLOBUS_SUCCESS)
        {
            goto error_ftp_mkdir;
        }
    }
    else if(url_mode == GLOBUS_GASS_COPY_URL_MODE_IO)
    {
        result = globus_l_gass_copy_mkdir_file(url);
        if(result != GLOBUS_SUCCESS)
        {
            goto error_file_mkdir;
        }
    }
    else
    {
        result = globus_error_put(
            globus_error_construct_string(
                GLOBUS_GASS_COPY_MODULE,
                GLOBUS_NULL,
                "[%s]: unsupported URL scheme: %s",
                myname,
                url));
        goto error_exit;
    }
    
    return GLOBUS_SUCCESS;
    
error_ftp_mkdir:
error_file_mkdir:

error_exit:

    return result;
    
}


static
globus_result_t
globus_l_gass_copy_mkdir_ftp(
    globus_gass_copy_handle_t *         handle,
    char *                              url,
    globus_gass_copy_attr_t *           attr)
{
    globus_result_t                     result;
    globus_l_gass_copy_glob_info_t      info;
    
    info.callbacks_left = 1;
    info.err = GLOBUS_NULL;
    globus_cond_init(&info.cond, GLOBUS_NULL);
    globus_mutex_init(&info.mutex, GLOBUS_NULL);
   
    result = globus_ftp_client_mkdir(
        &handle->ftp_handle,
        url,
        attr->ftp_attr,
        globus_l_gass_copy_ftp_client_op_done_callback,
        &info);
    if(result != GLOBUS_SUCCESS)
    {
        goto error_mkdir;
    }

    globus_mutex_lock(&info.mutex);
    while(info.callbacks_left > 0)
    {
        globus_cond_wait(&info.cond, &info.mutex);
    }
    globus_mutex_unlock(&info.mutex);

    if(info.err)
    {
        result = globus_error_put(info.err);
        info.err = GLOBUS_NULL;
    }
    
    if(result != GLOBUS_SUCCESS)
    {
        goto error_mkdir;
    }

    globus_cond_destroy(&info.cond);
    globus_mutex_destroy(&info.mutex);

    return GLOBUS_SUCCESS;

error_mkdir:
    globus_cond_destroy(&info.cond);
    globus_mutex_destroy(&info.mutex);

    return result;
}



static
globus_result_t
globus_l_gass_copy_mkdir_file(
    char *                              url)
{
    static char *   myname = "globus_l_gass_copy_mkdir_file";    
    int                                 rc;
    globus_url_t                        parsed_url;
    globus_result_t                     result;
    
    rc = globus_url_parse(url, &parsed_url);
    
    if(rc != 0)
    {
        result = globus_error_put(
            globus_error_construct_string(
                GLOBUS_GASS_COPY_MODULE,
                GLOBUS_NULL,
                "[%s]: error parsing url: "
                "globus_url_parse returned %d",
                myname,
                rc));
        goto error_url;
    }
    
    if(parsed_url.url_path == GLOBUS_NULL)
    {
        result = globus_error_put(
            globus_error_construct_string(
                GLOBUS_GASS_COPY_MODULE,
                GLOBUS_NULL,
                "[%s]: error parsing url: "
                "url has no path",
                myname));
        goto error_null_path;
    }
    
#ifndef TARGET_ARCH_WIN32
    rc = mkdir(parsed_url.url_path, 0777);
#else
    rc = mkdir(parsed_url.url_path);
#endif

    if(rc != 0)
    {
        result = globus_error_put(
            globus_error_construct_string(
                GLOBUS_GASS_COPY_MODULE,
                globus_error_construct_errno_error(
                    GLOBUS_GASS_COPY_MODULE,
                    NULL,
                    errno),
                "[%s]: error creating directory",
                myname));
        goto error_mkdir;
    }
   
    globus_url_destroy(&parsed_url); 
    return GLOBUS_SUCCESS;

error_mkdir:    
error_null_path:
    globus_url_destroy(&parsed_url);
    
error_url:

    return result;
    
}



static
globus_result_t
globus_l_gass_copy_cksm_ftp(
    globus_gass_copy_handle_t *         handle,
    char *                              url,
    globus_gass_copy_attr_t *           attr,
    char *                              cksm,
    globus_off_t                        offset,
    globus_off_t                        length,
    const char *                        algorithm,
    globus_gass_copy_callback_t         callback,
    void *                              callback_arg)
{
    globus_result_t                     result;
    globus_l_gass_copy_glob_info_t *    info;

    info = (globus_l_gass_copy_glob_info_t *)
        globus_calloc(sizeof(globus_l_gass_copy_glob_info_t), 1);
    
    info->callbacks_left = 1;
    info->err = GLOBUS_NULL;
    info->handle = handle;
    info->op_cb = callback;
    info->op_cb_arg = callback_arg;
    
    if(!callback)
    {
        globus_mutex_init(&info->mutex, GLOBUS_NULL);
        globus_cond_init(&info->cond, GLOBUS_NULL);
    }
    
    result = globus_ftp_client_cksm(
        &handle->ftp_handle,
        url,
        attr->ftp_attr,
        cksm,
        offset,
        length,
        algorithm,
        globus_l_gass_copy_ftp_client_cksm_done_callback,
        info);
    if(result != GLOBUS_SUCCESS)
    {
        goto error;
    }
    
    if(!callback)
    {
        globus_mutex_lock(&info->mutex);
        while(info->callbacks_left > 0)
        {
            globus_cond_wait(&info->cond, &info->mutex);
        }
        globus_mutex_unlock(&info->mutex);

        if(info->err)
        {
            result = globus_error_put(info->err);
            info->err = GLOBUS_NULL;
        }
        
        globus_cond_destroy(&info->cond);
        globus_mutex_destroy(&info->mutex);

        globus_free(info);
    }

    return GLOBUS_SUCCESS;
error:

    globus_free(info);
    return result;
}

#define GASS_COPY_CKSM_BUFSIZE 1024*1024

static
globus_result_t
globus_l_gass_copy_cksm_file(
    globus_gass_copy_handle_t *         handle,
    char *                              url,
    char *                              cksm,
    globus_off_t                        offset,
    globus_off_t                        length,
    const char *                        algorithm,
    globus_gass_copy_callback_t         callback,
    void *                              callback_arg)
{
    char *      myname = "globus_l_gass_copy_cksm_file";

    globus_url_t                        parsed_url;
    globus_result_t                     result;
    int                                 rc;

    EVP_MD_CTX *                        mdctx;
    const EVP_MD *                      md;
    char *                              cksmptr;
    unsigned char                       md_value[EVP_MAX_MD_SIZE];
    char                                cksm_buff[CKSM_SIZE];
    unsigned int                        md_len;

    char                                buf[GASS_COPY_CKSM_BUFSIZE];

    int                                 i;
    int                                 fd;
    int                                 n;
    globus_off_t                        count;
    globus_off_t                        read_left;

    rc = globus_url_parse_loose(url, &parsed_url);
    if(rc != 0)
    {
        result = globus_error_put(
            globus_error_construct_string(
                GLOBUS_GASS_COPY_MODULE,
                GLOBUS_NULL,
                "[%s]: error parsing url: "
                "globus_url_parse returned %d",
                myname,
                url));
        goto error_url;
    }

    if(parsed_url.url_path == GLOBUS_NULL)
    {
        result = globus_error_put(
            globus_error_construct_string(
                GLOBUS_GASS_COPY_MODULE,
                GLOBUS_NULL,
                "[%s]: error parsing url: "
                "url has no path",
                myname));
        goto error_fd;
    }    
       
    read_left = length;
    if(length >= 0)
    {
        count = (read_left > GASS_COPY_CKSM_BUFSIZE) ? 
            GASS_COPY_CKSM_BUFSIZE : read_left;
    }
    else
    {
        count = GASS_COPY_CKSM_BUFSIZE;
    }
    
    fd = open(parsed_url.url_path, O_RDONLY);        
    if(fd < 0)
    {
        result = globus_error_put(
            globus_error_construct_string(
                GLOBUS_GASS_COPY_MODULE,
                GLOBUS_NULL,
                "[%s]: error opening checksum file %s",
                myname,
                parsed_url.url_path));
        goto error_fd;
    }

    if (lseek(fd, offset, SEEK_SET) == -1)
    {
        result = globus_error_put(
            globus_error_construct_string(
                GLOBUS_GASS_COPY_MODULE,
                GLOBUS_NULL,
                "[%s]: error (%d) seeking checksum file %s",
                myname,
                errno,
                parsed_url.url_path));
        goto error_seek;
    }

    OpenSSL_add_all_algorithms();
    md = EVP_get_digestbyname(algorithm);
    if (!md)
    {
        /*
         * try again with uppercase algorithm name.
         */
        char *                          p;
        char *                          alg = strdup(algorithm);
        for(p = alg; *p != '\0'; p++)
        {
            *p = toupper(*p);
        }
        md = EVP_get_digestbyname(alg);
        free(alg);
    }
    if (!md)
    {
        result = globus_error_put(globus_error_construct_string(
                GLOBUS_GASS_COPY_MODULE,
                GLOBUS_NULL, 
                "Unable to use checksum algorithm %s", 
                algorithm));
        goto error_seek;
    }

    mdctx = EVP_MD_CTX_create();
    EVP_DigestInit_ex(mdctx, md, NULL);

    while((n = read(fd, buf, count)) > 0)
    {
        if(length >= 0)
        {
            read_left -= n;
            count = (read_left > GASS_COPY_CKSM_BUFSIZE) ? GASS_COPY_CKSM_BUFSIZE : read_left;
        }

        EVP_DigestUpdate(mdctx, buf, n);
    }

    EVP_DigestFinal_ex(mdctx, md_value, &md_len);
    EVP_MD_CTX_destroy(mdctx);
    
    close(fd);
        
    cksmptr = cksm_buff;
    for (i = 0; i < md_len; i++)
    {
       sprintf(cksmptr, "%02x", md_value[i]);
       cksmptr++;
       cksmptr++;
    }
    *cksmptr = '\0';
    
    strncpy(cksm, cksm_buff, sizeof(cksm_buff));
    
    globus_url_destroy(&parsed_url);
    
    if(callback)
    {
        callback(callback_arg, handle, NULL);
    }
    return GLOBUS_SUCCESS;

error_seek:
    close(fd);
error_fd:
    globus_url_destroy(&parsed_url);

error_url:

    return result;
}

/* Determine the type of the url and call checksum functions above accordingly
 * */

globus_result_t
globus_gass_copy_cksm(
    globus_gass_copy_handle_t *         handle,
    char *                              url,
    globus_gass_copy_attr_t *           attr,
    globus_off_t                        offset,
    globus_off_t                        length,
    const char *                        algorithm,
    char *                              cksm)
{
    return globus_gass_copy_cksm_async(
        handle,
        url,
        attr,
        offset,
        length,
        algorithm,
        cksm,
        NULL,
        NULL);
}

globus_result_t
globus_gass_copy_cksm_async(
    globus_gass_copy_handle_t *         handle,
    char *                              url,
    globus_gass_copy_attr_t *           attr,
    globus_off_t                        offset,
    globus_off_t                        length,
    const char *                        algorithm,
    char *                              cksm,
    globus_gass_copy_callback_t         callback,
    void *                              callback_arg)
{
    static char *   myname = "globus_gass_copy_cksm";    

    globus_result_t                     result;
    globus_gass_copy_url_mode_t         url_mode;
    
    result = globus_gass_copy_get_url_mode(url, &url_mode);

    if(result != GLOBUS_SUCCESS)
    {
        goto error_exit;
    }        

    if(url_mode == GLOBUS_GASS_COPY_URL_MODE_FTP)
    {
        result = globus_l_gass_copy_cksm_ftp(
            handle,
            url,
            attr,
            cksm,
            offset,
            length,
            algorithm,
            callback,
            callback_arg);
    
        if(result != GLOBUS_SUCCESS)
        {
            goto error_ftp_cksm;
        }
    }
    else if(url_mode == GLOBUS_GASS_COPY_URL_MODE_IO)
    {
        result = globus_l_gass_copy_cksm_file(
            handle,
            url,
            cksm, 
            offset,
            length,
            algorithm,
            callback,
            callback_arg);
        if(result != GLOBUS_SUCCESS)
        {
            goto error_file_cksm;
        }
    }
    else
    {
        result = globus_error_put(
            globus_error_construct_string(
                GLOBUS_GASS_COPY_MODULE,
                GLOBUS_NULL,
                "[%s]: unsupported URL scheme: %s",
                myname,
                url));
        goto error_exit;
    }

    return GLOBUS_SUCCESS;

error_ftp_cksm:
error_file_cksm:
error_exit:
        
    return result;
        
}

#ifdef _WIN32
static
int globus_l_win_fnmatch(
    const char * pattern,
    const char * filename)
{
    globus_list_t *possible_matches_filenames = NULL;
    globus_list_t *possible_matches_patterns = NULL;
    int matched = 0;
    
    globus_list_insert(&possible_matches_filenames, (void *) filename);
    globus_list_insert(&possible_matches_patterns, (void *) pattern);

    while (!globus_list_empty(possible_matches_filenames))
    {
        const char * filenamep;
        const char * patternp;

        filenamep = (const char *) globus_list_remove(
                &possible_matches_filenames, possible_matches_filenames);
        patternp = (const char *) globus_list_remove(
                &possible_matches_patterns, possible_matches_patterns);

        while (*patternp && *filenamep)
        {
            while (*patternp && *filenamep && 
                    *patternp != '*' &&
                    *patternp != '[' &&
                    (*patternp == *filenamep ||
                     *patternp == '?'))
            {
                patternp++;
                filenamep++;
            }
            if (*patternp == '[' && *filenamep)
            {
                const char * start_of_class =  ++patternp;
                const char * end_of_class = start_of_class;
                const char * classp;
                while (*end_of_class && *end_of_class != ']')
                {
                    end_of_class++;
                }
                for (classp = start_of_class; classp < end_of_class; classp++)
                {
                    if (*classp == *filenamep)
                    {
                        filenamep++;
                        patternp = end_of_class + 1;
                        break;
                    }
                }
            }
            else if (*patternp == '*' && *filenamep)
            {
                while (*patternp == '*')
                {
                    patternp++;
                }
                for (; *filenamep; filenamep++)
                {
                    globus_list_insert(&possible_matches_filenames,
                            (void *) filenamep);
                    globus_list_insert(&possible_matches_patterns,
                            (void *) patternp);
                    if (*filenamep == '/')
                    {
                        break;
                    }
                }
                goto hit_wildcard;
            }
            else
            {
                /* No match */
                break;
            }
        }
        if (*patternp == 0 && *filenamep == 0)
        {
            matched = 1;
            break;
        }
hit_wildcard:;
    }
    while (!globus_list_empty(possible_matches_filenames))
    {
        globus_list_remove(
            &possible_matches_filenames, possible_matches_filenames);
    }
    while (!globus_list_empty(possible_matches_patterns))
    {
        globus_list_remove(
                &possible_matches_patterns, possible_matches_patterns);
    }
    return matched;
}
/* win_fnmatch() */
#endif
