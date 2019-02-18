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

#include "globus_i_gridftp_server.h"

/**
 * should select logging based on configuration.  log output funcs should
 * still be usable before this and will output to stderr.
 *
 * if this fails, just print to stderr.
 */


static globus_logging_handle_t          globus_l_gfs_log_handle = NULL;
static FILE *                           globus_l_gfs_log_file = NULL;
static FILE *                           globus_l_gfs_transfer_log_file = NULL;
static globus_bool_t                    globus_l_gfs_log_events = GLOBUS_FALSE;
static int                              globus_l_gfs_log_mask = 0;


int
globus_l_gfs_log_matchlevel(
    char *                              tag)
{
    int                                 out = 0;
    GlobusGFSName(globus_l_gfs_log_matchlevel);
    GlobusGFSDebugEnter();

    if(strcasecmp(tag, "ERROR") == 0)
    {
        out = GLOBUS_GFS_LOG_ERR;
    }
    else if(strcasecmp(tag, "WARN") == 0)
    {
        out = GLOBUS_GFS_LOG_WARN;
    }
    else if(strcasecmp(tag, "INFO") == 0)
    {
        out = GLOBUS_GFS_LOG_INFO;
    }
    else if(strcasecmp(tag, "TRANSFER") == 0)
    {
        out = GLOBUS_GFS_LOG_TRANSFER;
    }
    else if(strcasecmp(tag, "DUMP") == 0)
    {
        out = GLOBUS_GFS_LOG_DUMP;
    }
    else if(strcasecmp(tag, "ALL") == 0)
    {
        out = GLOBUS_GFS_LOG_ALL;
    }

    GlobusGFSDebugExit();
    return out;
}


void
globus_i_gfs_log_open()
{
    char *                              module;
    char *                              module_str;
    globus_logging_module_t *           log_mod;
    void *                              log_arg = NULL;
    char *                              logfilename = NULL;
    char *                              log_filemode = NULL;
    char *                              logunique = NULL;
    char *                              log_level = NULL;
    int                                 log_mask = 0;
    char *                              ptr;
    int                                 len;
    int                                 ctr;
    char *                              tag;
    globus_result_t                     result;
    globus_reltime_t                    flush_interval;
    globus_size_t                       buffer;
    int                                 rc;
    GlobusGFSName(globus_i_gfs_log_open);
    GlobusGFSDebugEnter();

    GlobusTimeReltimeSet(flush_interval, 5, 0);
    buffer = 65536;

    /* parse user supplied log level string */
    log_level = globus_libc_strdup(globus_i_gfs_config_string("log_level"));
    if(log_level != NULL)
    {
        len = strlen(log_level);
        for(ctr = 0; ctr < len && isdigit(log_level[ctr]); ctr++);
        /* just a number, set log level to the supplied level || every level
            below */
        if(ctr == len)
        {
            log_mask = atoi(log_level);
            if(log_mask > 1)
            {
                log_mask |= (log_mask >> 1) | ((log_mask >> 1)  - 1);
            }
        }
        else
        {
            tag = log_level;
            while((ptr = strchr(tag, ',')) != NULL)
            {
                *ptr = '\0';
                log_mask |= globus_l_gfs_log_matchlevel(tag);
                tag = ptr + 1;
            }
            if(ptr == NULL)
            {
                log_mask |= globus_l_gfs_log_matchlevel(tag);
            }
        }
        globus_free(log_level);
    }

    module_str = globus_libc_strdup(globus_i_gfs_config_string("log_module"));
    module = module_str;
    if(module_str != NULL)
    {
        char *                          opts;
        char *                          end;
        globus_off_t                    tmp_off;

        end = module_str + strlen(module_str);
        ptr = strchr(module_str, ':');
        if(ptr != NULL)
        {
            *ptr = '\0';
            ptr++;

            do
            {
                opts = ptr;
                ptr = strchr(opts, ':');
                if(ptr)
                {
                    *ptr = '\0';
                    ptr++;
                    if(ptr >= end)
                    {
                        ptr = NULL;
                    }
                }
                if(strncasecmp(opts, "buffer=", 7) == 0)
                {
                    rc = globus_args_bytestr_to_num(
                        opts + 7, &tmp_off);
                    if(rc != 0)
                    {
                        fprintf(stderr, "Invalid value for log buffer\n");
                    }
                    if(tmp_off == 0)
                    {
                        log_mask |= GLOBUS_LOGGING_INLINE;
                    }
                    if(tmp_off < 2048)
                    {
                         buffer = 2048;
                    }
                    else
                    {
                        buffer = (globus_size_t) tmp_off;
                    }
                }
                else if(strncasecmp(opts, "interval=", 9) == 0)
                {
                    rc = globus_args_bytestr_to_num(
                        opts + 9, &tmp_off);
                    if(rc != 0)
                    {
                        fprintf(stderr,
                            "Invalid value for log flush interval\n");
                    }
                    GlobusTimeReltimeSet(flush_interval, (int) tmp_off, 0);
                }
                else
                {
                    fprintf(stderr, "Invalid log module option: %s\n", opts);
                }


            } while(ptr && *ptr);
        }
    }

    if(module == NULL || strcmp(module, "stdio") == 0)
    {
        log_mod = &globus_logging_stdio_module;
    }
    else if(strcmp(module, "syslog") == 0)
    {
        log_mod = &globus_logging_syslog_module;
        log_mask |= GLOBUS_LOGGING_INLINE;
        GlobusTimeReltimeSet(flush_interval, 0, 0);
    }
    else if(strcmp(module, "stdio_ng") == 0)
    {
        log_mod = &globus_logging_stdio_ng_module;
        globus_l_gfs_log_events = GLOBUS_TRUE;
        log_mask |= GLOBUS_GFS_LOG_INFO | 
            GLOBUS_GFS_LOG_WARN | GLOBUS_GFS_LOG_ERR;
    }
    else if(strcmp(module, "syslog_ng") == 0)
    {
        log_mod = &globus_logging_syslog_ng_module;
        globus_l_gfs_log_events = GLOBUS_TRUE;
        log_mask |= GLOBUS_GFS_LOG_INFO | 
            GLOBUS_GFS_LOG_WARN | GLOBUS_GFS_LOG_ERR;
        log_mask |= GLOBUS_LOGGING_INLINE;
        GlobusTimeReltimeSet(flush_interval, 0, 0);            
    }
    else
    {
        globus_libc_fprintf(stderr,
            "Invalid logging module specified, using stdio.\n");
        log_mod = &globus_logging_stdio_module;
    }

    if(log_mod == &globus_logging_stdio_module ||
        log_mod == &globus_logging_stdio_ng_module )
    {
        logfilename = globus_i_gfs_config_string("log_single");
        if(logfilename == NULL)
        {
            logunique = globus_i_gfs_config_string("log_unique");
            if(logunique != NULL)
            {
                logfilename = globus_common_create_string(
                    "%sgridftp.%d.log", logunique, getpid());
            }
        }
        if(logfilename != NULL)
        {
            mode_t                  oldmask;
            oldmask = umask(022);
            globus_l_gfs_log_file = fopen(logfilename, "a");
            umask(oldmask);
            if(globus_l_gfs_log_file == NULL)
            {
                if(!globus_i_gfs_config_bool("inetd"))
                {
                    globus_libc_fprintf(stderr,
                        "Unable to open %s for logging. "
                        "Using stderr instead.\n", logfilename);
                    globus_l_gfs_log_file = stderr;
                }
            }
            else
            {
                setvbuf(globus_l_gfs_log_file, NULL, _IOLBF, 0);
                if((log_filemode =
                    globus_i_gfs_config_string("log_filemode")) != NULL)
                {
                    int                     mode = 0;
                    
                    rc = -1;
                    mode = strtoul(log_filemode, NULL, 8);
                    if(mode > 0 || 
                        (log_filemode[0] == '0' && log_filemode[1] == '\0'))
                    {
                        rc = chmod(logfilename, mode);
                    }
                    
                    if(rc != 0)
                    {
                        globus_libc_fprintf(globus_l_gfs_log_file,
                            "WARNING: Not setting log file permissions. "
                            "Invalid log_filemode: %s\n", log_filemode);
                    }
                }
            }
        }

        if(globus_l_gfs_log_file == NULL)
        {
            globus_l_gfs_log_file = stderr;
        }

        log_arg = globus_l_gfs_log_file;

        if(logunique != NULL)
        {
            globus_free(logfilename);
        }
    }

    globus_l_gfs_log_mask = log_mask;
    
    if(!((log_mod == &globus_logging_stdio_module ||
        log_mod == &globus_logging_stdio_ng_module) && log_arg == NULL))
    {
        globus_logging_init(
            &globus_l_gfs_log_handle,
            &flush_interval,
            buffer,
            log_mask,
            log_mod,
            log_arg);
    }

    if((logfilename = globus_i_gfs_config_string("log_transfer")) != NULL)
    {
        mode_t                  oldmask;
        oldmask = umask(022);
        globus_l_gfs_transfer_log_file = fopen(logfilename, "a");
        umask(oldmask);
        if(globus_l_gfs_transfer_log_file == NULL)
        {
            if(!globus_i_gfs_config_bool("inetd"))
            {
                globus_libc_fprintf(stderr,
                    "Unable to open %s for transfer logging.\n", logfilename);
            }
        }
        else
        {
            setvbuf(globus_l_gfs_transfer_log_file, NULL, _IOLBF, 0);
            if((log_filemode = 
                globus_i_gfs_config_string("log_filemode")) != NULL)
            {
                int                     mode = 0;

                rc = -1;
                mode = strtoul(log_filemode, NULL, 8);
                if(mode > 0 || 
                    (log_filemode[0] == '0' && log_filemode[1] == '\0'))
                {
                    rc = chmod(logfilename, mode);
                }
                
                if(rc != 0)
                {
                    globus_libc_fprintf(globus_l_gfs_transfer_log_file,
                        "WARNING: Not setting log file permissions. "
                        "Invalid log_filemode: %s\n", log_filemode);
                }
            }
        }
    }
    else if(log_mask & GLOBUS_GFS_LOG_TRANSFER)
    {
        /* let the rest of the code know we want transfer logging */
        globus_gfs_config_set_ptr("log_transfer", "");
    }

    if(module_str)
    {
        globus_free(module_str);
    }

    GlobusGFSDebugExit();
}

void
globus_i_gfs_log_close(void)
{
    globus_list_t *                     list;
    GlobusGFSName(globus_i_gfs_log_close);
    GlobusGFSDebugEnter();

    if(globus_l_gfs_log_handle != NULL)
    {
        globus_logging_flush(globus_l_gfs_log_handle);
        /*
            NOTE: We do not destroy this handle.  At log-close time,
            there may be several other threads that try to subsequently
            log:
            - Watchdog callback for data / control channels (race condition)
            - DSI code during shutdown or threads.
            If they try to grab the destroyed mutex, they may deadlock.
            Since access to the pointer is not threadsafe, we cannot simply
            set it to NULL.
        globus_logging_destroy(globus_l_gfs_log_handle);
        */
    }
    if(globus_l_gfs_log_file != stderr && globus_l_gfs_log_file != NULL)
    {
        fclose(globus_l_gfs_log_file);
        globus_l_gfs_log_file = NULL;
    }
    if(globus_l_gfs_transfer_log_file != NULL)
    {
        fclose(globus_l_gfs_transfer_log_file);
        globus_l_gfs_transfer_log_file = NULL;
    }

    GlobusGFSDebugExit();
}

void
globus_gfs_log_message(
    globus_gfs_log_type_t               type,
    const char *                        format,
    ...)
{
    va_list                             ap;
    GlobusGFSName(globus_gfs_log_message);
    GlobusGFSDebugEnter();

    if(globus_l_gfs_log_handle != NULL && !globus_l_gfs_log_events)
    {
        va_start(ap, format);
        globus_logging_vwrite(globus_l_gfs_log_handle, type, format, ap);
        va_end(ap);
    }
    
    if(type == GLOBUS_GFS_LOG_ERR && globus_l_gfs_log_handle)
    {
        globus_logging_flush(globus_l_gfs_log_handle);
    }
        
    GlobusGFSDebugExit();
}

void
globus_gfs_log_exit_message(
    const char *                        format,
    ...)
{
    va_list                             ap;
    char *                              msg;
    GlobusGFSName(globus_gfs_log_exit_message);
    GlobusGFSDebugEnter();
    
    va_start(ap, format);
    msg = globus_common_v_create_string(format, ap);
    va_end(ap);

    if(globus_l_gfs_log_handle && globus_l_gfs_log_file != stderr)
    {
        globus_gfs_log_message(
            GLOBUS_GFS_LOG_ERR, 
            "Server configuration error. %s",
            msg);
    }

    if(globus_i_gfs_config_bool("inetd") || !globus_l_gfs_log_handle)
    {
        char *                          tmp;
        char *                          out_msg;
        tmp = globus_common_create_string(
            "Server configuration error.\n\n%s\nPlease notify administrator.",
            msg);
        out_msg = globus_gsc_string_to_959(500, tmp, " ");
        globus_libc_fprintf(stderr, "%s", out_msg);
        globus_free(tmp);
        globus_free(out_msg);
    }
    else
    {
        globus_libc_fprintf(stderr, "Server configuration error.\n%s", msg);
    }
    
    globus_free(msg);

    if(globus_l_gfs_log_handle)
    {
        globus_logging_flush(globus_l_gfs_log_handle);
    }

    GlobusGFSDebugExit();
}


void
globus_gfs_log_result(
    globus_gfs_log_type_t               type,
    const char *                        lead,
    globus_result_t                     result)
{
    char *                              message;
    GlobusGFSName(globus_gfs_log_result);
    GlobusGFSDebugEnter();

    if(result != GLOBUS_SUCCESS)
    {
        message = globus_error_print_friendly(globus_error_peek(result));
    }
    else
    {
        message = globus_libc_strdup("(unknown error)");
    }
    globus_gfs_log_message(type, "%s:\n%s\n", lead, message);
    globus_free(message);

    GlobusGFSDebugExit();
}

void
globus_gfs_log_exit_result(
    const char *                        lead,
    globus_result_t                     result)
{
    char *                              message;
    GlobusGFSName(globus_gfs_log_result);
    GlobusGFSDebugEnter();

    if(result != GLOBUS_SUCCESS)
    {
        message = globus_error_print_friendly(globus_error_peek(result));
    }
    else
    {
        message = globus_libc_strdup("(unknown error)");
    }
    globus_gfs_log_exit_message("%s:\n%s\n", lead, message);
    globus_free(message);

    GlobusGFSDebugExit();
}

void
globus_i_gfs_log_tr(
    char *                              msg,
    char                                from,
    char                                to)
{
    char *                              ptr;
    GlobusGFSName(globus_l_gfs_log_tr);
    GlobusGFSDebugEnter();

    ptr = strchr(msg, from);
    while(ptr != NULL)
    {
        *ptr = to;
        ptr = strchr(ptr, from);
    }
    GlobusGFSDebugExit();
}


void
globus_gfs_log_event(
    globus_gfs_log_type_t               type,
    globus_gfs_log_event_type_t         event_type,
    const char *                        event_name,
    globus_result_t                     result,
    const char *                        format,
    ...)
{
    va_list                             ap;
    char *                              msg;
    char *                              tmp = NULL;
    char *                              startend;
    char *                              status;
    char *                              message = NULL;
    GlobusGFSName(globus_gfs_log_event);
    GlobusGFSDebugEnter();

    if(globus_l_gfs_log_handle != NULL && globus_l_gfs_log_events)
    {
        if(format)
        {
            va_start(ap, format);
            tmp = globus_common_v_create_string(format, ap);
            va_end(ap);

            globus_i_gfs_log_tr(tmp, '\n', ' ');
        }

        if(result != GLOBUS_SUCCESS)
        {
            message = globus_error_print_friendly(globus_error_peek(result));
            globus_i_gfs_log_tr(message, '\n', ' ');
            globus_i_gfs_log_tr(message, '\"', '\'');
        }

        switch(event_type)
        {
            case GLOBUS_GFS_LOG_EVENT_START:
                startend = "start";
                status = NULL;
                break;
            case GLOBUS_GFS_LOG_EVENT_END:
                startend = "end";
                if(result == GLOBUS_SUCCESS)
                {
                    status = " status=0";
                }
                else
                {
                    status = " status=-1";
                }
                break;
            case GLOBUS_GFS_LOG_EVENT_MESSAGE:
                startend = "message";
                status = NULL;
                break;
            default:
                startend = "error";
                status = " status=-1";
                break;
        }

        msg = globus_common_create_string(
            "event=globus-gridftp-server%s%s.%s%s%s%s%s%s%s\n",
            event_name ? "." : "",
            event_name ? event_name : "",
            startend,
            tmp ? " " : "",
            tmp ? tmp : "",
            message ? " msg=\"" : "",
            message ? message : "",
            message ? "\"" : "",
            status ? status : "");

        globus_logging_write(globus_l_gfs_log_handle, type, msg);

        globus_free(msg);
        if(tmp)
        {
            globus_free(tmp);
        }
        if(message)
        {
            globus_free(message);
        }
    }

    GlobusGFSDebugExit();
}

char *
globus_i_gfs_log_create_transfer_event_msg(
    int                                 stripe_count,
    int                                 stream_count,
    char *                              dest_ip,
    globus_size_t                       blksize,
    globus_size_t                       tcp_bs,
    const char *                        fname,
    globus_off_t                        nbytes,
    char *                              type,
    char *                              username,
    char *                              retransmit_str,
    char *                              taskid)
{
    char *                              transfermsg;
    GlobusGFSName(globus_i_gfs_log_create_transfer_event_msg);
    GlobusGFSDebugEnter();

    transfermsg = globus_common_create_string(
        "localuser=%s "
        "file=%s "
        "tcpbuffer=%ld "
        "blocksize=%ld "
        "bytes=%"GLOBUS_OFF_T_FORMAT" "
        "streams=%d "
        "stripes=%d "
        "remoteIP=%s "
        "type=%s "
        "taskid=%s"
        "%s%s",
        username,
        fname,
        (long) tcp_bs,
        (long) blksize,
        nbytes,
        stream_count,
        stripe_count,
        dest_ip,
        type,
        taskid ? taskid : "none",
        retransmit_str ? " retrans=" : "",
        retransmit_str ? retransmit_str : "");

    GlobusGFSDebugExit();
    return transfermsg;
}

void
globus_i_gfs_log_transfer(
    int                                 stripe_count,
    int                                 stream_count,
    struct timeval *                    start_gtd_time,
    struct timeval *                    end_gtd_time,
    char *                              dest_ip,
    globus_size_t                       blksize,
    globus_size_t                       tcp_bs,
    const char *                        fname,
    globus_off_t                        nbytes,
    int                                 code,
    char *                              volume,
    char *                              type,
    char *                              username,
    char *                              retransmit_str,
    char *                              taskid)
{
    time_t                              start_time_time;
    time_t                              end_time_time;
    struct tm *                         tmp_tm_time;
    struct tm                           start_tm_time;
    struct tm                           end_tm_time;
    char                                out_buf[4096];
    long                                win_size;
    GlobusGFSName(globus_i_gfs_log_transfer);
    GlobusGFSDebugEnter();

    if(globus_l_gfs_transfer_log_file == NULL && 
        !(globus_l_gfs_log_mask & GLOBUS_GFS_LOG_TRANSFER))
    {
        goto err;
    }

    start_time_time = (time_t)start_gtd_time->tv_sec;
    tmp_tm_time = gmtime(&start_time_time);
    if(tmp_tm_time == NULL)
    {
        goto err;
    }
    start_tm_time = *tmp_tm_time;

    end_time_time = (time_t)end_gtd_time->tv_sec;
    tmp_tm_time = gmtime(&end_time_time);
    if(tmp_tm_time == NULL)
    {
        goto err;
    }
    end_tm_time = *tmp_tm_time;

    if(tcp_bs == 0)
    {
        win_size = 0;
    }
    else
    {
        win_size = tcp_bs;
    }

    snprintf(out_buf, sizeof(out_buf),
        "DATE=%04d%02d%02d%02d%02d%02d.%06d "
        "HOST=%s "
        "PROG=%s "
        "NL.EVNT=FTP_INFO "
        "START=%04d%02d%02d%02d%02d%02d.%06d "
        "USER=%s "
        "FILE=%s "
        "BUFFER=%ld "
        "BLOCK=%ld "
        "NBYTES=%"GLOBUS_OFF_T_FORMAT" "
        "VOLUME=%s "
        "STREAMS=%d "
        "STRIPES=%d "
        "DEST=[%s] "
        "TYPE=%s "
        "CODE=%d "
        "TASKID=%s"
        "%s%s\n",
        /* end time */
        end_tm_time.tm_year + 1900,
        end_tm_time.tm_mon + 1,
        end_tm_time.tm_mday,
        end_tm_time.tm_hour,
        end_tm_time.tm_min,
        end_tm_time.tm_sec,
        (int) end_gtd_time->tv_usec,
        globus_i_gfs_config_string("fqdn"),
        "globus-gridftp-server",
        /* start time */
        start_tm_time.tm_year + 1900,
        start_tm_time.tm_mon + 1,
        start_tm_time.tm_mday,
        start_tm_time.tm_hour,
        start_tm_time.tm_min,
        start_tm_time.tm_sec,
        (int) start_gtd_time->tv_usec,
        /* other args */
        username,
        fname,
        win_size,
        (long) blksize,
        nbytes,
        volume,
        stream_count,
        stripe_count,
        dest_ip,
        type,
        code,
        taskid ? taskid : "none",
        retransmit_str ? " retrans=" : "",
        retransmit_str ? retransmit_str : "");

    out_buf[sizeof(out_buf)-1] = '\0';

    if(globus_l_gfs_transfer_log_file != NULL)
    {
        fwrite(out_buf, 1, strlen(out_buf), globus_l_gfs_transfer_log_file);
    }
    if(globus_l_gfs_log_mask & GLOBUS_GFS_LOG_TRANSFER)
    {
        globus_gfs_log_message(
            GLOBUS_GFS_LOG_TRANSFER, "Transfer stats: %s", out_buf);
    }

    GlobusGFSDebugExit();
    return;

err:
    GlobusGFSDebugExitWithError();
}
