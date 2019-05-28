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

#include "globus_gridftp_server_control.h"
#include "globus_i_gridftp_server_control.h"
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <ctype.h>
//#include <sys/utsname.h>

/*
 *  These commands will only come in one at a time
 */

static void
globus_l_gsc_cmd_transfer(
    globus_i_gsc_cmd_wrapper_t *            wrapper);

#define globus_l_gsc_is_ftp_code(_code) ((_code) >= 400 && (_code) <= 599)


/*************************************************************************
 *                      simple commands
 *                      ---------------
 ************************************************************************/
static void
globus_l_gsc_cmd_all(
    globus_i_gsc_op_t *                 op,
    const char *                        full_command,
    char **                             cmd_a,
    int                                 argc,
    void *                              user_arg)
{
    /* do logging here */
    globus_gsc_959_finished_command(op, NULL);
}

static void
globus_l_gsc_cmd_stru(
    globus_i_gsc_op_t *                 op,
    const char *                        full_command,
    char **                             cmd_a,
    int                                 argc,
    void *                              user_arg)
{
    char *                              tmp_ptr;

    globus_i_gsc_log(op->server_handle, full_command,
        GLOBUS_GRIDFTP_SERVER_CONTROL_LOG_OTHER);
    tmp_ptr = cmd_a[1];
    if((tmp_ptr[0] == 'f' || tmp_ptr[0] == 'F') 
        && tmp_ptr[1] == '\0')
    {
        globus_gsc_959_finished_command(op, _FSMSL("200 STRU F ok.\r\n"));
    }
    else
    {
        globus_gsc_959_finished_command(
            op, _FSMSL("501 Syntax error in parameter.\r\n"));
    }
}

static void
globus_l_gsc_cmd_allo(
    globus_i_gsc_op_t *                 op,
    const char *                        full_command,
    char **                             cmd_a,
    int                                 argc,
    void *                              user_arg)
{
    int                                 sc;
    globus_off_t                        size;

    globus_i_gsc_log(op->server_handle, full_command,
        GLOBUS_GRIDFTP_SERVER_CONTROL_LOG_FILE_COMMANDS);

    sc = sscanf(cmd_a[1], "%"GLOBUS_OFF_T_FORMAT, &size);
    if(sc == 1)
    {
        op->server_handle->allocated_bytes = size;
        globus_gsc_959_finished_command(op, _FSMSL("200 ALLO command successful.\r\n"));
    }
    else
    {
        globus_gsc_959_finished_command(
            op, _FSMSL("501 Syntax error in parameters or arguments.\r\n"));
    }
}


/*
 *  simply pings the control channel
 */
static void
globus_l_gsc_cmd_noop(
    globus_i_gsc_op_t *                     op,
    const char *                            full_command,
    char **                                 cmd_a,
    int                                     argc,
    void *                                  user_arg)
{
    globus_i_gsc_log(op->server_handle, full_command,
        GLOBUS_GRIDFTP_SERVER_CONTROL_LOG_OTHER);
    globus_gsc_959_finished_command(op, _FSMSL("200 NOOP command successful.\r\n"));
}

static void
globus_l_gsc_cmd_pbsz(
    globus_i_gsc_op_t *                     op,
    const char *                            full_command,
    char **                                 cmd_a,
    int                                     argc,
    void *                                  user_arg)
{
    char *                                  msg;

    globus_i_gsc_log(op->server_handle, full_command,
        GLOBUS_GRIDFTP_SERVER_CONTROL_LOG_SECURITY);
    if(strlen(cmd_a[1]) > 10 || 
        (strlen(cmd_a[1]) == 10 && strcmp(cmd_a[1], "4294967296") >= 0))
    {
        msg = globus_common_create_string(_FSMSL("501 Bad value for PBSZ: %s\r\n"),
            cmd_a[1]);
    }
    else
    {
        msg = globus_common_create_string(_FSMSL("200 PBSZ=%s\r\n"), cmd_a[1]);
    }
    globus_gsc_959_finished_command(op, msg);
    globus_free(msg);
}

static void
globus_l_gsc_cmd_dcau(
    globus_i_gsc_op_t *                     op,
    const char *                            full_command,
    char **                                 cmd_a,
    int                                     argc,
    void *                                  user_arg)
{
    char *                                  tmp_ptr;
    char *                                  msg;

    tmp_ptr = cmd_a[1];
    if(tmp_ptr[1] != '\0')
    {
        globus_gsc_959_finished_command(op, _FSMSL("504 Bad DCAU mode.\r\n"));
        return;
    }

    globus_i_gsc_log(op->server_handle, full_command,
        GLOBUS_GRIDFTP_SERVER_CONTROL_LOG_SECURITY);

    *tmp_ptr = toupper(*tmp_ptr);
    switch(*tmp_ptr)
    {
        case 'S':
            /*
            if(op->server_handle->del_cred == NULL)
            {
                globus_gsc_959_finished_command(
                    op, _FSMSL("504 No delegated credential.\r\n"));
            }
            else */
            if(argc < 3)
            {
                globus_gsc_959_finished_command(
                    op, _FSMSL("501 DCAU S expected subject.\r\n"));
            }
            else
            {
                op->server_handle->dcau = *tmp_ptr;
                if(op->server_handle->dcau_subject != NULL)
                {
                    globus_free(op->server_handle->dcau_subject);
                }
                op->server_handle->dcau_subject = strdup(cmd_a[2]);
                globus_i_guc_command_data_destroy(op->server_handle);
                globus_gsc_959_finished_command(op, _FSMSL("200 DCAU S.\r\n"));
            }
            break;

        case 'A':
            /* if no del cred return error else fall through */
            /*
            if(op->server_handle->del_cred == NULL)
            {
                globus_gsc_959_finished_command(
                    op, _FSMSL("504 No delegated credential.\r\n"));
                break;
            }
            */
        case 'N':
            if(argc != 2)
            {
                globus_gsc_959_finished_command(
                    op, _FSMSL("501 Bad Parameter to DCAU.\r\n"));
            }
            else
            {
                msg = globus_common_create_string(_FSMSL("200 DCAU %c.\r\n"), *tmp_ptr);
                op->server_handle->dcau = *tmp_ptr;
                globus_i_guc_command_data_destroy(op->server_handle);
                globus_gsc_959_finished_command(op, msg);
                globus_free(msg);
            }
            break;

        default:
            globus_gsc_959_finished_command(op, _FSMSL("501 Bad DCAU mode.\r\n"));
            break;
    }
}

static void
globus_l_gsc_cmd_trev(
    globus_i_gsc_op_t *                     op,
    const char *                            full_command,
    char **                                 cmd_a,
    int                                     argc,
    void *                                  user_arg)
{
    char *                                  event_name;
    int                                     frequency;
    int                                     sc;

    globus_i_gsc_log(op->server_handle, full_command,
        GLOBUS_GRIDFTP_SERVER_CONTROL_LOG_TRANSFER_STATE);
    for(event_name = cmd_a[1]; *event_name != '\0'; event_name++)
    {
        *event_name = toupper(*event_name);
    }
    event_name = cmd_a[1];
    sc = sscanf(cmd_a[2], "%d", &frequency);
    if(sc != 1)
    {
        globus_gsc_959_finished_command(op, _FSMSL("501 Bad parameter mode.\r\n"));
    }

    if(strcmp(event_name, "RESTART") == 0)
    {
        op->server_handle->opts.restart_frequency = frequency;
        globus_gsc_959_finished_command(op, _FSMSL("200 Command Successful.\r\n"));
    }
    else if(strcmp(event_name, "PERF") == 0)
    {
        op->server_handle->opts.perf_frequency = frequency;
        globus_gsc_959_finished_command(op, _FSMSL("200 Command Successful.\r\n"));
    }
    else
    {
        globus_gsc_959_finished_command(op, _FSMSL("502 Unsupported event.\r\n"));
    }
}

static void
globus_l_gsc_cmd_prot(
    globus_i_gsc_op_t *                     op,
    const char *                            full_command,
    char **                                 cmd_a,
    int                                     argc,
    void *                                  user_arg)
{
    char *                                  tmp_ptr;
    char *                                  msg;

    globus_i_gsc_log(op->server_handle, full_command,
        GLOBUS_GRIDFTP_SERVER_CONTROL_LOG_SECURITY);

    tmp_ptr = cmd_a[1];
    if(tmp_ptr[1] != '\0')
    {
        msg = globus_common_create_string(
            _FSMSL("536 %s protection level not supported.\r\n"), cmd_a[1]);
        globus_gsc_959_finished_command(op, msg);
        globus_free(msg);
        return;
    }

    *tmp_ptr = toupper(*tmp_ptr);
    switch(*tmp_ptr)
    {
        case 'P':
        case 'S':
        /*  no need to restrict this here
            if(op->server_handle->del_cred == NULL)
            {
                msg = globus_common_create_string(
                    _FSMSL("536 %s protection level not supported.\r\n"), cmd_a[1]);
                break;
            }
        */
        case 'C':
            msg = globus_common_create_string(
                _FSMSL("200 Protection level set to %c.\r\n"), *tmp_ptr);
            op->server_handle->prot = *tmp_ptr;
            globus_i_guc_command_data_destroy(op->server_handle);
            break;

        default:
            msg = globus_common_create_string(
                _FSMSL("536 %s protection level not supported.\r\n"), cmd_a[1]);
            break;
    }

    globus_gsc_959_finished_command(op, msg);
    globus_free(msg);
}

static void
globus_l_gsc_cmd_mdtm_cb(
    globus_i_gsc_op_t *                     op,
    globus_gridftp_server_control_response_t response_type,
    char *                                  response_msg,
    char *                                  path,
    char *                                  abs_path,
    globus_gridftp_server_control_stat_t *  stat_info,
    int                                     stat_count,
    uid_t                                   uid,
    void *                                  user_arg)
{
    int                                     code;
    char *                                  tmp_ptr;
    struct tm *                             tm;
    char *                                  msg;
    GlobusGridFTPServerName(globus_l_gsc_cmd_mdtm_cb);

    if(response_type != GLOBUS_GRIDFTP_SERVER_CONTROL_RESPONSE_SUCCESS)
    {
        switch(response_type)
        {
            default:
                if (globus_l_gsc_is_ftp_code(response_type))
                {
                    code = response_type;
                    msg = strdup(response_msg);
                    response_msg = NULL;
                }
                else
                {
                    code = 500;
                    msg = globus_libc_strdup(_FSMSL("Command failed"));
                }
                break;
        }
    }
    else
    {
        tm = gmtime(&stat_info[0].mtime);
        code = 213;
        msg =  globus_common_create_string(
            "%04d%02d%02d%02d%02d%02d",
            tm->tm_year+1900, tm->tm_mon+1, tm->tm_mday, 
            tm->tm_hour, tm->tm_min, tm->tm_sec);
    }

    if(response_msg != NULL)
    {
        tmp_ptr = msg;
        msg = globus_common_create_string("%s : %s", msg, response_msg);
        free(tmp_ptr);
    }
    tmp_ptr = globus_gsc_string_to_959(code, msg, NULL);
    globus_gsc_959_finished_command(op, tmp_ptr);
    globus_free(tmp_ptr);
    globus_free(msg);
}

static void
globus_l_gsc_cmd_mdtm(
    globus_i_gsc_op_t *                     op,
    const char *                            full_command,
    char **                                 cmd_a,
    int                                     argc,
    void *                                  user_arg)
{
    globus_result_t                         res;

    globus_i_gsc_log(op->server_handle, full_command,
        GLOBUS_GRIDFTP_SERVER_CONTROL_LOG_LIST);

    res = globus_i_gsc_resource_query(
            op,
            cmd_a[1],
            GLOBUS_GRIDFTP_SERVER_CONTROL_RESOURCE_FILE_ONLY,
            globus_l_gsc_cmd_mdtm_cb,
            NULL);
    if(res != GLOBUS_SUCCESS)
    {
        globus_gsc_959_finished_command(op, _FSMSL("500 Command not supported.\r\n"));
    }
}

/*
 *  mode
 */
static void
globus_l_gsc_cmd_mode(
    globus_i_gsc_op_t *                     op,
    const char *                            full_command,
    char **                                 cmd_a,
    int                                     argc,
    void *                                  user_arg)
{
    char *                                  msg;
    char *                                  tmp_ptr;
    char                                    ch;

    globus_i_gsc_log(op->server_handle, full_command,
        GLOBUS_GRIDFTP_SERVER_CONTROL_LOG_TRANSFER_STATE);
    ch = (char)toupper((int)cmd_a[1][0]);
    if(strchr(op->server_handle->modes, ch) == NULL)
    {
        tmp_ptr = strstr(full_command, "\r\n");
        if(tmp_ptr != NULL)
        {
            *tmp_ptr = '\0';
        }
        msg = globus_common_create_string(
            _FSMSL("501 '%s' unrecognized transfer mode.\r\n"), full_command);
    }
    else
    {
        msg = globus_common_create_string(_FSMSL("200 Mode set to %c.\r\n"), ch);
        op->server_handle->mode = ch;
    }
    if(msg == NULL)
    {
        globus_i_gsc_command_panic(op);
    }
    else
    {
        globus_gsc_959_finished_command(op, msg);
        globus_free(msg);
    }
}

/*
 *  type
 */
static void
globus_l_gsc_cmd_type(
    globus_i_gsc_op_t *                     op,
    const char *                            full_command,
    char **                                 cmd_a,
    int                                     argc,
    void *                                  user_arg)
{
    char                                    ch;
    char *                                  msg;
    GlobusGridFTPServerName(globus_l_gsc_cmd_type);

    globus_i_gsc_log(op->server_handle, full_command,
        GLOBUS_GRIDFTP_SERVER_CONTROL_LOG_TRANSFER_STATE);
    ch = (char)toupper((int)cmd_a[1][0]);
    if(strchr(op->server_handle->types, ch) == NULL)
    {
        msg = globus_common_create_string(
            _FSMSL("501 '%s' unrecognized type.\r\n"), full_command);
    }
    else
    {
        msg = globus_common_create_string(_FSMSL("200 Type set to %c.\r\n"), ch);
        op->server_handle->type = ch;
    }
    if(msg == NULL)
    {
        globus_i_gsc_command_panic(op);
    }
    else
    {
        globus_gsc_959_finished_command(op, msg);
        globus_free(msg);
    }
}

/*
 *  lang
 */
static void
globus_l_gsc_cmd_lang(
    globus_i_gsc_op_t *                     op,
    const char *                            full_command,
    char **                                 cmd_a,
    int                                     argc,
    void *                                  user_arg)
{
    char *                                  lang = NULL;

    
    char *                                  msg;

    globus_i_gsc_log(op->server_handle, full_command,
        GLOBUS_GRIDFTP_SERVER_CONTROL_LOG_TRANSFER_STATE);
    if (cmd_a[1]==NULL)
    {
	    /*nothing specified after LANG, keep default*/
        msg = globus_common_create_string(_FSMSL("200 lang set to %s.\r\n"), lang);
	op->server_handle->lang = lang;
    }
    else
    {
        lang = strdup(cmd_a[1]);
    }

    if(lang == NULL)
    {
        msg = globus_common_create_string(_FSMSL("200 lang set to %s.\r\n"), "EN");
    }
    else
    {
	/*Check if it is a supported language*/
	/*As internationalization continues, this will be discovered
	 * from configuration--hardcoded for now
	 */
	if (strcmp(lang, "EN")==0)
	{	
           msg = globus_common_create_string(_FSMSL("200 lang set to %s.\r\n"), lang);
           op->server_handle->lang = lang;
	}
	else
	{
           msg = globus_common_create_string(
            _FSMSL("501 '%s' unrecognized language.\r\n"), full_command);
            free(lang);
	}
    }
    if(msg == NULL)
    {
        globus_i_gsc_command_panic(op);
    }
    else
    {
        globus_gsc_959_finished_command(op, msg);
        globus_free(msg);
    }
}

/*************************************************************************
 *                      directory functions
 *                      -------------------
 ************************************************************************/
/*
 *  PWD
 */
static void
globus_l_gsc_cmd_pwd(
    globus_i_gsc_op_t *                     op,
    const char *                            full_command,
    char **                                 cmd_a,
    int                                     argc,
    void *                                  user_arg)
{
    char *                                  msg;
    GlobusGridFTPServerName(globus_l_gsc_cmd_pwd);

    globus_i_gsc_log(op->server_handle, full_command,
        GLOBUS_GRIDFTP_SERVER_CONTROL_LOG_OTHER);
    msg = globus_common_create_string(
        _FSMSL("257 \"%s\" is current directory.\r\n"), op->server_handle->cwd);
    if(msg == NULL)
    {
        globus_i_gsc_command_panic(op);
    }
    else
    {
        globus_gsc_959_finished_command(op, msg);
        globus_free(msg);
    }
}

/*
 *  CWD
 */
static void
globus_l_gsc_cmd_cwd_cb(
    globus_i_gsc_op_t *                     op,
    globus_gridftp_server_control_response_t response_type,
    char *                                  response_msg,
    char *                                  path,
    char *                                  abs_path,
    globus_gridftp_server_control_stat_t *  stat_info,
    int                                     stat_count,
    uid_t                                   uid,
    void *                                  user_arg)
{
    int                                     code;
    char *                                  msg = NULL;
    char *                                  tmp_ptr;
    GlobusGridFTPServerName(globus_l_gsc_cmd_cwd_cb);

    /*
     *  decide what message to send
     */
    if(response_type != GLOBUS_GRIDFTP_SERVER_CONTROL_RESPONSE_SUCCESS ||
        stat_count < 1)
    {
        switch(response_type)
        {
            case GLOBUS_GRIDFTP_SERVER_CONTROL_RESPONSE_PATH_INVALID:
                code = 550;
                msg = globus_common_create_string(
                    _FSMSL("%s: No such file or directory."), path);
                break;

            case GLOBUS_GRIDFTP_SERVER_CONTROL_RESPONSE_ACCESS_DENIED:
                code = 553;
                msg = globus_common_create_string(_FSMSL("Permission denied."));
                break;

            default:
                code = 550;
                msg = globus_common_create_string(
                    _FSMSL("%s: Could not change directory."), path);
                break;
        }
    }
    else if(!S_ISDIR(stat_info->mode))
    {
        code = 550;
        msg = globus_common_create_string(_FSMSL("%s: Not a directory."), path);
    }
    else
    {
        /*
        * ignore permission checks for CWD.  We will use the success of the stat
        * to indicate validity, since this is merely a virtual operation.
        * specifically, allows this to function with linux filesystem ACL
        */
        {
            if(op->server_handle->cwd != NULL)
            {
                globus_free(op->server_handle->cwd);
            }
            op->server_handle->cwd = globus_libc_strdup(path);
            code = 250;
            msg = globus_libc_strdup(_FSMSL("CWD command successful."));
        }
    }
    if(msg == NULL)
    {
        globus_i_gsc_command_panic(op);
        goto err;
    }
    if(response_msg != NULL)
    {
        tmp_ptr = msg;
        msg = globus_common_create_string("%s : %s", msg, response_msg);
        free(tmp_ptr);
    }
    tmp_ptr = globus_gsc_string_to_959(code, msg, NULL);
    globus_gsc_959_finished_command(op, tmp_ptr);
    globus_free(tmp_ptr);
    globus_free(msg);

    return;

  err:

    if(msg != NULL)
    {
        globus_free(msg);
    }
}

static void
globus_l_gsc_cmd_cwd(
    globus_i_gsc_op_t *                     op,
    const char *                            full_command,
    char **                                 cmd_a,
    int                                     argc,
    void *                                  user_arg)
{
    globus_result_t                         res;
    int                                     mask = GLOBUS_GRIDFTP_SERVER_CONTROL_RESOURCE_FILE_ONLY;
    char *                                  path = NULL;
    GlobusGridFTPServerName(globus_l_gsc_cmd_cwd);

    globus_i_gsc_log(op->server_handle, full_command,
        GLOBUS_GRIDFTP_SERVER_CONTROL_LOG_OTHER);
    if(strcmp(cmd_a[0], "CDUP") == 0 && argc == 1)
    {
        path = globus_i_gsc_concat_path(op->server_handle, "..");
        if(path == NULL)
        {
            globus_i_gsc_command_panic(op);
            goto err;
        }
    }
    else if(argc == 2)
    {
        path = globus_i_gsc_concat_path(op->server_handle, cmd_a[1]);
        if(path == NULL)
        {
            globus_gsc_959_finished_command(op,
                _FSMSL("550 Could not change directory.\r\n"));
            goto err;
        }
    }
    else
    {
        globus_gsc_959_finished_command(op,
            _FSMSL("550 Could not change directory.\r\n"));
        goto err;
    }

    res = globus_i_gsc_resource_query(
            op,
            path,
            mask,
            globus_l_gsc_cmd_cwd_cb,
            NULL);
    if(res != GLOBUS_SUCCESS)
    {
        goto err;
    }
    globus_free(path);

    return;

  err:
    if(path != NULL)
    {
        globus_free(path);
    }
}

/*
 *  STAT
 */
static void
globus_l_gsc_cmd_stat_cb(
    globus_i_gsc_op_t *                     op,
    globus_gridftp_server_control_response_t response_type,
    char *                                  response_msg,
    char *                                  path,
    char *                                  abs_path,
    globus_gridftp_server_control_stat_t *  stat_info,
    int                                     stat_count,
    uid_t                                   uid,
    void *                                  user_arg)
{
    int                                     code = 500;
    char *                                  msg;
    char *                                  tmp_ptr;
    char *                                  preline;
    globus_bool_t                           partial = GLOBUS_FALSE;
    GlobusGridFTPServerName(globus_l_gsc_cmd_stat_cb);

    if(response_type != GLOBUS_GRIDFTP_SERVER_CONTROL_RESPONSE_SUCCESS &&
        response_type != GLOBUS_GRIDFTP_SERVER_CONTROL_RESPONSE_PARTIAL_SUCCESS)
    {
        switch(response_type)
        {
            case GLOBUS_GRIDFTP_SERVER_CONTROL_RESPONSE_PATH_INVALID:
                code = 550;
                msg = globus_common_create_string(
                    _FSMSL("No such file or directory."));
                break;

            case GLOBUS_GRIDFTP_SERVER_CONTROL_RESPONSE_ACCESS_DENIED:
                code = 553;
                msg = globus_common_create_string(
                    _FSMSL("Permission denied."));
                break;

            default:
                if (globus_l_gsc_is_ftp_code(response_type))
                {
                    code = response_type;
                    msg = strdup(response_msg);
                    response_msg = NULL;
                }
                else
                {
                    code = 500;
                    msg = globus_libc_strdup(_FSMSL("Command failed"));
                }
                break;
        }
        preline = NULL;
    }
    else
    {
        if(user_arg == NULL)
        {
            code = 213;
            tmp_ptr = globus_i_gsc_list_single_line(stat_info);
            msg =  globus_common_create_string(
                _FSMSL("%d-status of %s\r\n %s\r\n%d End.\r\n"),
                code, op->path, tmp_ptr, code);
            globus_free(tmp_ptr);
        }
        else if((intptr_t) user_arg == GLOBUS_L_GSC_OP_TYPE_MLSD)
        {
            if(response_type == 
                GLOBUS_GRIDFTP_SERVER_CONTROL_RESPONSE_PARTIAL_SUCCESS)
            {
                partial = GLOBUS_TRUE;
                code = 150;
            }
            else
            {
                code = 250;
            }
            tmp_ptr = globus_i_gsc_mlsx_line(
               stat_info, 
               stat_count, 
               op->server_handle->opts.mlsx_fact_str, 
               uid,
               NULL,
               abs_path,
               GLOBUS_TRUE);
            msg =  globus_common_create_string(
               _FSMSL("%d-Contents of %s\r\n%s%d End.\r\n"), 
               code, 
               op->path, 
               tmp_ptr,
               code);                
            globus_free(tmp_ptr);
        }
        else
        {
            code = 250;
            /* for mlst we want the requested path, not just the filename */
            if(stat_info->name != NULL)
            {
                globus_free(stat_info->name);
            }
            stat_info->name = globus_libc_strdup(path);
            tmp_ptr = globus_i_gsc_mlsx_line_single(
                op->server_handle->opts.mlsx_fact_str, uid, stat_info, NULL, abs_path, GLOBUS_TRUE);
            msg =  globus_common_create_string(
                _FSMSL("%d-status of %s\r\n %s\r\n%d End.\r\n"),
                code, op->path, tmp_ptr, code);
            globus_free(tmp_ptr);
        }
        preline = " ";
    }

    if(response_msg != NULL)
    {
        tmp_ptr = msg;
        msg = globus_common_create_string("%s : %s", tmp_ptr, response_msg);
        free(tmp_ptr);
    }
    if(code >= 400)
    {
        tmp_ptr = globus_gsc_string_to_959(code, msg, preline);
    }
    else
    {
        tmp_ptr = msg;
    }
    if(partial)
    {
        globus_mutex_lock(&op->server_handle->mutex);
        globus_i_gsc_intermediate_reply(op, tmp_ptr);
        globus_mutex_unlock(&op->server_handle->mutex);
        globus_mutex_unlock(&op->stat_lock);
    }
    else
    {
        if((intptr_t) user_arg == GLOBUS_L_GSC_OP_TYPE_MLSD)
        {
            globus_mutex_unlock(&op->stat_lock);
        }
        globus_gsc_959_finished_command(op, tmp_ptr);
    }
    if(code >= 400)
    {
        globus_free(tmp_ptr);
    }
    globus_free(msg);

}

static void
globus_l_gsc_cmd_stat(
    globus_i_gsc_op_t *                     op,
    const char *                            full_command,
    char **                                 cmd_a,
    int                                     argc,
    void *                                  user_arg)
{
    /* these are really just place holders in the list */
    int                                     mask = GLOBUS_GRIDFTP_SERVER_CONTROL_RESOURCE_FILE_ONLY;
    char *                                  msg = NULL;
    char *                                  path;
    globus_result_t                         res;
    GlobusGridFTPServerName(globus_l_gsc_cmd_stat);

    globus_i_gsc_log(op->server_handle, full_command,
        GLOBUS_GRIDFTP_SERVER_CONTROL_LOG_OTHER);
    if(argc == 1 && user_arg == 0)
    {
        msg = globus_common_create_string(
                _FSMSL("212 GridFTP server status.\r\n"));
        if(msg == NULL)
        {
            globus_i_gsc_command_panic(op);
            goto err;
        }
        globus_gsc_959_finished_command(op, msg);
        globus_free(msg);
    }
    else
    {
        if((intptr_t) user_arg == GLOBUS_L_GSC_OP_TYPE_MLSD)
        {
            mask = GLOBUS_GRIDFTP_SERVER_CONTROL_RESOURCE_DIRECTORY_LIST;
        }
        if(argc != 2)
        {
            path = op->server_handle->cwd;
        }
        else
        {
            path = cmd_a[1];
        }
        res = globus_i_gsc_resource_query(
                op,
                path,
                mask,
                globus_l_gsc_cmd_stat_cb,
                user_arg);
        if(res != GLOBUS_SUCCESS)
        {
            globus_gsc_959_finished_command(
                op, _FSMSL("500 Command not supported.\r\n"));
        }
    }

    return;

  err:
    return;
}

/*
 *  size and mdtm
 */
static void
globus_l_gsc_cmd_size_cb(
    globus_i_gsc_op_t *                     op,
    globus_gridftp_server_control_response_t response_type,
    char *                                  response_msg,
    char *                                  path,
    char *                                  abs_path,
    globus_gridftp_server_control_stat_t *  stat_info,
    int                                     stat_count,
    uid_t                                   uid,
    void *                                  user_arg)
{
    int                                     code;
    char *                                  tmp_ptr;
    char *                                  msg = NULL;
    GlobusGridFTPServerName(globus_l_gsc_cmd_size_cb);

    if(response_type != GLOBUS_GRIDFTP_SERVER_CONTROL_RESPONSE_SUCCESS ||
        stat_count < 1)
    {
        switch(response_type)
        {
            case GLOBUS_GRIDFTP_SERVER_CONTROL_RESPONSE_PATH_INVALID:
                code = 550;
                msg = globus_common_create_string(
                    _FSMSL("No such file."));
                break;

            case GLOBUS_GRIDFTP_SERVER_CONTROL_RESPONSE_ACCESS_DENIED:
                code = 553;
                msg = globus_common_create_string(
                    _FSMSL("Permission denied."));
                break;

            default:
                if (globus_l_gsc_is_ftp_code(response_type))
                {
                    code = response_type;
                    msg = strdup(response_msg);
                    response_msg = NULL;
                }
                else
                {
                    code = 550;
                    msg = globus_libc_strdup(_FSMSL("Command failed"));
                }
                break;
        }
    }
    else
    {
        code = 213;
        msg = globus_common_create_string(
            "%"GLOBUS_OFF_T_FORMAT, stat_info->size);
    }
    if(msg == NULL)
    {
        globus_i_gsc_command_panic(op);
        goto err;
    }
    if(response_msg != NULL)
    {
        tmp_ptr = msg;
        msg = globus_common_create_string("%s : %s", msg, response_msg);
        free(tmp_ptr);
    }
    tmp_ptr = globus_gsc_string_to_959(code, msg, NULL);
    globus_gsc_959_finished_command(op, tmp_ptr);
    globus_free(tmp_ptr);
    globus_free(msg);
    
    return;
    
  err:
    if(msg != NULL)
    {
        globus_free(msg);
    }
}

static void
globus_l_gsc_cmd_size(
    globus_i_gsc_op_t *                     op,
    const char *                            full_command,
    char **                                 cmd_a,
    int                                     argc,
    void *                                  user_arg)
{
    /* these are really just place holders in the list */
    char *                                  path = NULL;
    int                                     mask = GLOBUS_GRIDFTP_SERVER_CONTROL_RESOURCE_FILE_ONLY;
    globus_result_t                         res;
    GlobusGridFTPServerName(globus_l_gsc_cmd_size);

    globus_i_gsc_log(op->server_handle, full_command,
        GLOBUS_GRIDFTP_SERVER_CONTROL_LOG_FILE_COMMANDS);
    path = strdup(cmd_a[1]);
    if(path == NULL)
    {
        globus_i_gsc_command_panic(op);
        goto err;
    }
    res = globus_i_gsc_resource_query(
        op,
        path,
        mask,
        globus_l_gsc_cmd_size_cb,
        NULL);
    if(res != GLOBUS_SUCCESS)
    {
        globus_i_gsc_command_panic(op);
        goto err;
    }
    globus_free(path);

    return;

  err:
    if(path != NULL)
    {
        globus_free(path);
    }
}

/*
 *  quit
 */
static void
globus_l_gsc_cmd_quit(
    globus_i_gsc_op_t *                     op,
    const char *                            full_command,
    char **                                 cmd_a,
    int                                     argc,
    void *                                  user_arg)
{
    globus_i_gsc_server_handle_t *          server_handle;
    GlobusGridFTPServerName(globus_l_gsc_cmd_quit);

    GlobusGridFTPServerDebugInternalEnter();

    server_handle = op->server_handle;

    globus_i_gsc_log(op->server_handle, full_command,
        GLOBUS_GRIDFTP_SERVER_CONTROL_LOG_SECURITY);

    globus_gsc_959_terminate(op, _FSMSL("221 Goodbye.\r\n"));

    GlobusGridFTPServerDebugInternalExit();
}

/*************************************************************************
 *                      authentication commands
 *                      -----------------------
 ************************************************************************/
/*
 *   USER
 */
static void
globus_l_gsc_cmd_user(
    globus_i_gsc_op_t *                     op,
    const char *                            full_command,
    char **                                 cmd_a,
    int                                     argc,
    void *                                  user_arg)
{
    char *                                  msg;
    GlobusGridFTPServerName(globus_l_gsc_cmd_user);

    globus_i_gsc_log(op->server_handle, full_command,
        GLOBUS_GRIDFTP_SERVER_CONTROL_LOG_SECURITY);
    if(op->server_handle->username != NULL)
    {
        globus_free(op->server_handle->username);
        op->server_handle->username = NULL;
    }
    op->server_handle->username = globus_libc_strdup(cmd_a[1]);
    msg = globus_common_create_string(
        _FSMSL("331 Password required for %s.\r\n"), op->server_handle->username);
    if(msg == NULL)
    {
        goto err;
    }
    globus_gsc_959_finished_command(op, msg);
    globus_free(msg);
    return;

  err:
    if(op->server_handle->username != NULL)
    {
        globus_free(op->server_handle->username);
    }
    globus_i_gsc_command_panic(op);
}

static void
globus_l_gsc_auth_cb(
    globus_i_gsc_op_t *                     op,
    globus_gridftp_server_control_response_t response_type,
    char *                                  response_msg,
    void *                                  user_arg)
{
    int                                     code;
    char *                                  msg;
    char *                                  tmp_ptr;

    if(response_type == GLOBUS_GRIDFTP_SERVER_CONTROL_RESPONSE_SUCCESS)
    {
        if(op->server_handle->post_auth_banner == NULL)
        {
            code = 230;
            msg = globus_common_create_string(
                _FSMSL("User %s logged in."),
                op->server_handle->username);
        }
        else
        {
            code = 230;
            msg = globus_common_create_string(
                _FSMSL("User %s logged in.\n%s"),
                op->server_handle->username,
                op->server_handle->post_auth_banner);
        }
    }
    else
    {
        code = 530;
        msg = globus_common_create_string(_FSMSL("Login incorrect."));
    }
    globus_i_gsc_log(op->server_handle, op->command,
        GLOBUS_GRIDFTP_SERVER_CONTROL_LOG_SECURITY);
    if(response_msg != NULL)
    {
        tmp_ptr = msg;
        msg = globus_common_create_string("%s : %s", msg, response_msg);
        free(tmp_ptr);
    }
    tmp_ptr = globus_gsc_string_to_959(code, msg, NULL);
    globus_gsc_959_finished_command(op, tmp_ptr);
    globus_free(tmp_ptr);
    globus_free(msg);
}

/*
 *  pass
 */
static void
globus_l_gsc_cmd_pass(
    globus_i_gsc_op_t *                     op,
    const char *                            full_command,
    char **                                 cmd_a,
    int                                     argc,
    void *                                  user_arg)
{
    char *                                  msg = NULL;
    globus_result_t                         res;
    GlobusGridFTPServerName(globus_l_gsc_cmd_pass);

    /*
     *  if user name has not yet been supplied return error message
     */
    globus_i_gsc_log(op->server_handle, full_command,
        GLOBUS_GRIDFTP_SERVER_CONTROL_LOG_SECURITY);
    if(op->server_handle->username == NULL)
    {
        msg = _FSMSL("503 Login with USER first.\r\n");
        if(msg == NULL)
        {
            goto err;
        }
        globus_gsc_959_finished_command(op, msg);
    }
    else
    {
        res = globus_i_gsc_authenticate(
            op,
            op->server_handle->username,
            (argc == 2) ? cmd_a[1] : "",
            globus_l_gsc_auth_cb,
            NULL);
        if(res != GLOBUS_SUCCESS)
        {
            goto err;
        }
    }

    return;

  err:
    globus_i_gsc_command_panic(op);
}

/*
 *  syst
 */
static void
globus_l_gsc_cmd_syst(
    globus_i_gsc_op_t *                     op,
    const char *                            full_command,
    char **                                 cmd_a,
    int                                     argc,
    void *                                  user_arg)
{
    GlobusGridFTPServerName(globus_l_gsc_cmd_syst);

    globus_i_gsc_log(op->server_handle, full_command,
        GLOBUS_GRIDFTP_SERVER_CONTROL_LOG_OTHER);
    
    globus_gsc_959_finished_command(op, "215 UNIX Type: L8\r\n");

    return;
}

static void
globus_l_gsc_cmd_feat(
    globus_i_gsc_op_t *                     op,
    const char *                            full_command,
    char **                                 cmd_a,
    int                                     argc,
    void *                                  user_arg)
{
    char *                                  msg;
    char *                                  tmp_ptr;
    globus_list_t *                         list;

    globus_i_gsc_log(op->server_handle, full_command,
        GLOBUS_GRIDFTP_SERVER_CONTROL_LOG_OTHER);
    msg = globus_libc_strdup(_FSMSL("211-Extensions supported\r\n"));
    for(list = op->server_handle->feature_list;
        !globus_list_empty(list);
        list = globus_list_rest(list))
    {
        tmp_ptr = globus_common_create_string("%s %s\r\n", msg,
            (char *)globus_list_first(list));
        globus_free(msg);
        msg = tmp_ptr;
    }
    tmp_ptr = globus_common_create_string(_FSMSL("%s211 End.\r\n"), msg);
    globus_free(msg);

    globus_gsc_959_finished_command(op, tmp_ptr);
    globus_free(tmp_ptr);
}

/*
 *  help
 */
static void
globus_l_gsc_cmd_help(
    globus_i_gsc_op_t *                     op,
    const char *                            full_command,
    char **                                 cmd_a,
    int                                     argc,
    void *                                  user_arg)
{
    int                                     ctr;
    char *                                  msg;
    char *                                  arg;
    GlobusGridFTPServerName(globus_l_gsc_cmd_help);

    globus_i_gsc_log(op->server_handle, full_command,
        GLOBUS_GRIDFTP_SERVER_CONTROL_LOG_OTHER);
    /* general help */
    if(argc == 1 || (argc == 2 && strcmp(cmd_a[0], "SITE") == 0))
    {
        arg = NULL;
    }
    else
    {
        if(strcmp(cmd_a[0], "SITE") == 0)
        {
            arg = globus_libc_strdup(cmd_a[2]);
        }
        else
        {
            arg = globus_libc_strdup(cmd_a[1]);
        }
        for(ctr = 0; ctr < strlen(arg); ctr++)
        {
            arg[ctr] = toupper(arg[ctr]);
        }
    }

    msg = globus_i_gsc_get_help(op->server_handle, arg);
    if(arg != NULL)
    {
        globus_free(arg);
    }
    if(msg == NULL)
    {
        goto err;
    }

    globus_gsc_959_finished_command(op, msg);
    globus_free(msg);

    return;

  err:
    globus_i_gsc_command_panic(op);
}

/*
 * opts
 */
static void
globus_l_gsc_cmd_opts(
    globus_i_gsc_op_t *                     op,
    const char *                            full_command,
    char **                                 cmd_a,
    int                                     argc,
    void *                                  user_arg)
{
    globus_bool_t                           done = GLOBUS_FALSE;
    int                                     tmp_i;
    char *                                  msg;
    char *                                  tmp_ptr;
    globus_i_gsc_handle_opts_t *            opts;
    GlobusGridFTPServerName(globus_l_gsc_cmd_opts);

    globus_i_gsc_log(op->server_handle, full_command,
        GLOBUS_GRIDFTP_SERVER_CONTROL_LOG_OTHER);
    opts = &op->server_handle->opts;

    for(tmp_ptr = cmd_a[1]; *tmp_ptr != '\0'; tmp_ptr++)
    {
        *tmp_ptr = toupper(*tmp_ptr);
    }
    
    if(argc != 3)
    {
        msg = _FSMSL("500 OPTS failed.\r\n");
    }
    else if(strcmp("RETR", cmd_a[1]) == 0)
    {
        for(tmp_ptr = cmd_a[2]; *tmp_ptr != '\0'; tmp_ptr++)
        {
            *tmp_ptr = tolower(*tmp_ptr);
        }
    
        tmp_ptr = cmd_a[2];

        done = GLOBUS_FALSE;
        while(!done && *tmp_ptr != '\0')
        {
            while(*tmp_ptr == ' ') tmp_ptr++;

            msg = _FSMSL("200 OPTS Command Successful.\r\n");
            if(strncmp(tmp_ptr,
                "stripelayout=", sizeof("stripelayout=")-1) == 0)
            {
                tmp_ptr += sizeof("stripelayout=")-1;
                if(strncmp(
                    tmp_ptr, "partitioned;", sizeof("partitioned;")-1) == 0)
                {
                    opts->layout = GLOBUS_GSC_LAYOUT_TYPE_PARTITIONED;
                }
                else if(strncmp(tmp_ptr, "blocked;", sizeof("blocked;")-1) == 0)
                {
                    opts->layout = GLOBUS_GSC_LAYOUT_TYPE_BLOCKED;
                }
                else
                {
                    msg = _FSMSL("500 OPTS failed.\r\n");
                    done = GLOBUS_TRUE;
                }
            }
            else if(
                strncmp(tmp_ptr, "parallelism=", sizeof("parallelism=")-1) == 0)
            {
                tmp_ptr += sizeof("parallelism=")-1;
                if(sscanf(tmp_ptr, "%d,%*d,%*d;", &tmp_i) == 1)
                {
                    opts->parallelism = tmp_i;
                }
                else
                {
                    msg = _FSMSL("500 OPTS failed.\r\n");
                    done = GLOBUS_TRUE;
                }
            }
            else if(
                strncmp(tmp_ptr, "packetsize=", sizeof("packetsize=")-1) == 0)
            {
                if(sscanf(tmp_ptr, "%d;", &tmp_i) == 1)
                {
                    opts->packet_size = tmp_i;
                }
                else
                {
                    msg = _FSMSL("500 OPTS failed.\r\n");
                    done = GLOBUS_TRUE;
                }
            }
            else if(
                strncmp(tmp_ptr, "windowsize=", sizeof("windowsize=")-1) == 0)
            {
                if(sscanf(tmp_ptr, "%d;", &tmp_i) == 1)
                {
                    opts->send_buf = tmp_i;
                }
                else
                {
                    msg = _FSMSL("500 OPTS failed.\r\n");
                    done = GLOBUS_TRUE;
                }
            }
            else if(
                strncmp(tmp_ptr, "blocksize=", sizeof("blocksize=")-1) == 0)
            {
                if(sscanf(tmp_ptr, "blocksize=%d;", &tmp_i) == 1)
                {
                    opts->block_size = tmp_i;
                }
                else
                {
                    msg = _FSMSL("500 OPTS failed.\r\n");
                    done = GLOBUS_TRUE;
                }
            }
            else if(
                strncmp(tmp_ptr, "markers=", sizeof("markers=")-1) == 0)
            {
                if(sscanf(tmp_ptr, "markers=%d;", &tmp_i) == 1)
                {
                    char * freq_str = globus_common_create_string("%d", tmp_i);
                    op->server_handle->opts.retr_perf_frequency = tmp_i;                    
                    globus_libc_setenv("GFS_RETR_MARKERS", freq_str, 1);
                }
                else
                {
                    msg = _FSMSL("500 OPTS failed.\r\n");
                    done = GLOBUS_TRUE;
                }
            }
            else
            {
                msg = _FSMSL("500 OPTS failed.\r\n");
                done = GLOBUS_TRUE;
            }
            tmp_ptr = strchr(tmp_ptr, ';');
            if(tmp_ptr == NULL)
            {
                msg = _FSMSL("500 OPTS failed.\r\n");
                done = GLOBUS_TRUE;
            }
            tmp_ptr++;
        }
    }
    else if(strcmp("CKSM", cmd_a[1]) == 0)
    {
        for(tmp_ptr = cmd_a[2]; *tmp_ptr != '\0'; tmp_ptr++)
        {
            *tmp_ptr = tolower(*tmp_ptr);
        }
        msg = _FSMSL("200 OPTS Command Successful.\r\n");
        if(sscanf(cmd_a[2], "markers=%d;", &tmp_i) == 1)
        {
            char * freq_str = globus_common_create_string("%d", tmp_i);
            globus_libc_setenv("GFS_CKSM_MARKERS", freq_str, 1);
        }
        else
        {
            msg = _FSMSL("500 OPTS failed.\r\n");
        }
    }
    else if(strcmp("PASV", cmd_a[1]) == 0 || 
        strcmp("SPAS", cmd_a[1]) == 0 ||
        strcmp("EPSV", cmd_a[1]) == 0)
    {
        for(tmp_ptr = cmd_a[2]; *tmp_ptr != '\0'; tmp_ptr++)
        {
            *tmp_ptr = tolower(*tmp_ptr);
        }
        msg = _FSMSL("200 OPTS Command Successful.\r\n");
        if(sscanf(cmd_a[2], "allowdelayed=%d", &tmp_i) == 1)
        {
            opts->delayed_passive = tmp_i;
        }
        else
        {
            msg = _FSMSL("500 OPTS failed.\r\n");
        }
    }
    else if(strcmp("MLST", cmd_a[1]) == 0 || 
            strcmp("MLSD", cmd_a[1]) == 0 ||
            strcmp("MLSR", cmd_a[1]) == 0)
    {
        globus_bool_t                   mlsr_options = GLOBUS_FALSE;
        
        for(tmp_ptr = cmd_a[2]; *tmp_ptr != '\0'; tmp_ptr++)
        {
            *tmp_ptr = tolower(*tmp_ptr);
        }
        /** Pull out specific MLSR options */
        if(strcmp("MLSR", cmd_a[1]) == 0)
        {
            if(strstr(cmd_a[2], "onerror=continue"))
            {
                opts->mlsr_traversal_options |= GLOBUS_GFS_TRAVERSAL_CONTINUE;
                mlsr_options = GLOBUS_TRUE;
            } 
            else if(strstr(cmd_a[2], "onerror=fail"))
            {
                opts->mlsr_traversal_options &= ~GLOBUS_GFS_TRAVERSAL_CONTINUE;
                mlsr_options = GLOBUS_TRUE;
            }            
            if(strstr(cmd_a[2], "traversesymboliclinks=true"))
            {
                opts->mlsr_traversal_options |= GLOBUS_GFS_TRAVERSAL_FOLLOW_SYMLINKS;
                mlsr_options = GLOBUS_TRUE;
            } 
            else if(strstr(cmd_a[2], "traversesymboliclinks=false"))
            {
                opts->mlsr_traversal_options &= ~GLOBUS_GFS_TRAVERSAL_FOLLOW_SYMLINKS;
                mlsr_options = GLOBUS_TRUE;
            }            
        }
        /** Parse out common options */
        tmp_ptr = opts->mlsx_fact_str;
        if(strstr(cmd_a[2], "type"))
        {
            *tmp_ptr = GLOBUS_GSC_MLSX_FACT_TYPE;
            tmp_ptr++;
        }
        if(strstr(cmd_a[2], "modify"))
        {
            *tmp_ptr = GLOBUS_GSC_MLSX_FACT_MODIFY;
            tmp_ptr++;
        }
        if(strstr(cmd_a[2], "charset"))
        {
            *tmp_ptr = GLOBUS_GSC_MLSX_FACT_CHARSET;
            tmp_ptr++;
        }
        if(strstr(cmd_a[2], "size"))
        {
            *tmp_ptr = GLOBUS_GSC_MLSX_FACT_SIZE;
            tmp_ptr++;
        }
        if(strstr(cmd_a[2], "perm"))
        {
            *tmp_ptr = GLOBUS_GSC_MLSX_FACT_PERM;
            tmp_ptr++;
        }
        if(strstr(cmd_a[2], "unix.mode"))
        {
            *tmp_ptr = GLOBUS_GSC_MLSX_FACT_UNIXMODE;
            tmp_ptr++;
        }
        if(strstr(cmd_a[2], "unix.owner"))
        {
            *tmp_ptr = GLOBUS_GSC_MLSX_FACT_UNIXOWNER;
            tmp_ptr++;
        }
        if(strstr(cmd_a[2], "unix.group"))
        {
            *tmp_ptr = GLOBUS_GSC_MLSX_FACT_UNIXGROUP;
            tmp_ptr++;
        }
        if(strstr(cmd_a[2], "unix.slink"))
        {
            *tmp_ptr = GLOBUS_GSC_MLSX_FACT_UNIXSLINK;
            tmp_ptr++;
        }
        if(strstr(cmd_a[2], "unique"))
        {
            *tmp_ptr = GLOBUS_GSC_MLSX_FACT_UNIQUE;
            tmp_ptr++;
        }
        if(strstr(cmd_a[2], "unix.uid"))
        {
            *tmp_ptr = GLOBUS_GSX_MLSX_FACT_UNIXUID;
            tmp_ptr++;
        } 
        if(strstr(cmd_a[2], "unix.gid"))
        {
            *tmp_ptr = GLOBUS_GSX_MLSX_FACT_UNIXGID;
            tmp_ptr++;
        }
        if(strstr(cmd_a[2], "x.count"))
        {
            *tmp_ptr = GLOBUS_GSC_MLSX_FACT_XCOUNT;
            tmp_ptr++;
        }
        *tmp_ptr = '\0';
        msg = _FSMSL("200 OPTS Command Successful.\r\n");
        if(tmp_ptr != opts->mlsx_fact_str || !mlsr_options)
        {
            *tmp_ptr = '\0';
        }
    }
    else
    {
        msg = _FSMSL("500 OPTS failed.\r\n");
    }

    globus_gsc_959_finished_command(op, msg);
}

/*
 *
 */
static void
globus_l_gsc_cmd_sbuf(
    globus_i_gsc_op_t *                     op,
    const char *                            full_command,
    char **                                 cmd_a,
    int                                     argc,
    void *                                  user_arg)
{
    int                                     sc;
    int                                     tmp_i;
    GlobusGridFTPServerName(globus_l_gsc_cmd_sbuf);

    globus_i_gsc_log(op->server_handle, full_command,
        GLOBUS_GRIDFTP_SERVER_CONTROL_LOG_TRANSFER_STATE);
    if(argc != 2)
    {
        globus_gsc_959_finished_command(op, _FSMSL("502 Invalid Parameter.\r\n"));
    }
    else
    {
        sc = sscanf(cmd_a[1], "%d", &tmp_i);
        if(sc != 1)
        {
            globus_gsc_959_finished_command(
                op, _FSMSL("502 Invalid Parameter.\r\n"));
        }
        else
        {
            op->server_handle->opts.send_buf = tmp_i;
            op->server_handle->opts.receive_buf = tmp_i;

            globus_gsc_959_finished_command(
                op, _FSMSL("200 SBUF Command Successful.\r\n"));
        }
    }
}

/*
 *
 */
static void
globus_l_gsc_cmd_site_sbuf(
    globus_i_gsc_op_t *                 op,
    const char *                        full_command,
    char **                             cmd_a,
    int                                 argc,
    void *                              user_arg)
{
    int                                 tmp_i;
    int                                 sc;

    globus_i_gsc_log(op->server_handle, full_command,
        GLOBUS_GRIDFTP_SERVER_CONTROL_LOG_TRANSFER_STATE);
    sc = sscanf(cmd_a[2], "%d", &tmp_i);
    if(sc != 1)
    {
        globus_gsc_959_finished_command(op,
                _FSMSL("501 Syntax error in parameters or arguments.\r\n"));
    }
    else
    {
        op->server_handle->opts.send_buf = tmp_i;
        op->server_handle->opts.receive_buf = tmp_i;
        globus_gsc_959_finished_command(op, _FSMSL("200 Site Command Successful.\r\n"));
    }
}

static void
globus_l_gsc_cmd_site_receive_buf(
    globus_i_gsc_op_t *                 op,
    const char *                        full_command,
    char **                             cmd_a,
    int                                 argc,
    void *                              user_arg)
{
    int                                 tmp_i;
    int                                 sc;

    globus_i_gsc_log(op->server_handle, full_command,
        GLOBUS_GRIDFTP_SERVER_CONTROL_LOG_TRANSFER_STATE);
    sc = sscanf(cmd_a[2], "%d", &tmp_i);
    if(sc != 1)
    {
        globus_gsc_959_finished_command(op,
                _FSMSL("501 Syntax error in parameters or arguments.\r\n"));
    }
    else
    {
        op->server_handle->opts.receive_buf = tmp_i;
        globus_gsc_959_finished_command(op, _FSMSL("200 Site Command Successful.\r\n"));
    }
}

static void
globus_l_gsc_cmd_site_send_buf(
    globus_i_gsc_op_t *                 op,
    const char *                        full_command,
    char **                             cmd_a,
    int                                 argc,
    void *                              user_arg)
{
    int                                 tmp_i;
    int                                 sc;

    globus_i_gsc_log(op->server_handle, full_command,
        GLOBUS_GRIDFTP_SERVER_CONTROL_LOG_TRANSFER_STATE);
    sc = sscanf(cmd_a[2], "%d", &tmp_i);
    if(sc != 1)
    {
        globus_gsc_959_finished_command(op,
                _FSMSL("501 Syntax error in parameters or arguments.\r\n"));
    }
    else
    {
        op->server_handle->opts.send_buf = tmp_i;
        globus_gsc_959_finished_command(op, _FSMSL("200 Site Command Successful.\r\n"));
    }
}

/*
 *
 */
static void
globus_l_gsc_cmd_site_fault(
    globus_i_gsc_op_t *                 op,
    const char *                        full_command,
    char **                             cmd_a,
    int                                 argc,
    void *                              user_arg)
{
    int                                 len;
    int                                 ctr;
  
    globus_i_gsc_log(op->server_handle, full_command,
        GLOBUS_GRIDFTP_SERVER_CONTROL_LOG_SITE);

    if(op->server_handle->fault_cmd != NULL)
    {
        globus_free(op->server_handle->fault_cmd);
    }
    op->server_handle->fault_cmd = globus_libc_strdup(cmd_a[2]);

    len = strlen(op->server_handle->fault_cmd);
    for(ctr = 0; ctr < len; ctr++)
    {
        op->server_handle->fault_cmd[ctr] = 
            toupper(op->server_handle->fault_cmd[ctr]);
    }
    
    globus_gsc_959_finished_command(
        op, "200 Site Command Successful.\r\n");

}

static void
globus_l_gsc_cmd_rest(
    globus_i_gsc_op_t *                     op,
    const char *                            full_command,
    char **                                 cmd_a,
    int                                     argc,
    void *                                  user_arg)
{
    globus_range_list_t                     range_list;
    globus_off_t                            start_offset;
    globus_off_t                            end_offset;
    int                                     sc;
    char *                                  tmp_ptr;

    globus_i_gsc_log(op->server_handle, full_command,
        GLOBUS_GRIDFTP_SERVER_CONTROL_LOG_TRANSFER_STATE);

    globus_range_list_init(&range_list);
    /* mode s */
    if(strchr(cmd_a[1], '-') == NULL)
    {
        sc = sscanf(cmd_a[1], "%"GLOBUS_OFF_T_FORMAT, &end_offset);
        if(sc != 1)
        {
            globus_gsc_959_finished_command(op, _FSMSL("501 bad parameter.\r\n"));
            globus_range_list_destroy(range_list);
            return;
        }

        globus_range_list_insert(range_list, 0, end_offset);
    }
    /* mode e */
    else
    {
        tmp_ptr = cmd_a[1];
        while(tmp_ptr != NULL)
        {
            sc = sscanf(tmp_ptr, 
                "%"GLOBUS_OFF_T_FORMAT"-%"GLOBUS_OFF_T_FORMAT, 
                &start_offset, &end_offset);
            if(sc != 2)
            {
                globus_gsc_959_finished_command(
                    op, _FSMSL("501 bad paremeter.\r\n"));
                globus_range_list_destroy(range_list);
                return;
            }

            globus_range_list_insert(
                range_list, start_offset, end_offset - start_offset);
            tmp_ptr = strchr(tmp_ptr, ',');
            if(tmp_ptr)
            {
                tmp_ptr++;
            }
        }
    }
    if(op->server_handle->range_list != NULL)
    {
        globus_range_list_destroy(op->server_handle->range_list);
    }
    op->server_handle->range_list = range_list;
    globus_gsc_959_finished_command(op, 
        _FSMSL("350 Restart Marker OK. Send STORE or RETR to initiate transfer.\r\n"));
}

/*************************************************************************
 *                  data connection esstablishement
 *                  -------------------------------
 ************************************************************************/
static void
globus_l_gsc_cmd_pasv_cb(
    globus_i_gsc_op_t *                     op,
    globus_gridftp_server_control_response_t response_type,
    char *                                  response_msg,
    const char **                           cs,
    int                                     addr_count,
    void *                                  user_arg)
{
    int                                     ctr;
    char *                                  tmp_ptr;
    char *                                  host;
    int                                     host_ip[16];
    int                                     ip_count;
    unsigned short                          port;
    int                                     sc;
    char *                                  msg = NULL;
    char *                                  err_msg = NULL;
    globus_i_gsc_cmd_wrapper_t *            wrapper = NULL;
    int                                     err_code;
    GlobusGridFTPServerName(globus_l_gsc_cmd_pasv_cb);

    wrapper = (globus_i_gsc_cmd_wrapper_t *) user_arg;
    wrapper->op = op;

    if(response_type != GLOBUS_GRIDFTP_SERVER_CONTROL_RESPONSE_SUCCESS)
    {
        switch(response_type)
        {
          case GLOBUS_GRIDFTP_SERVER_CONTROL_RESPONSE_DATA_CONN_AUTH:
            err_code = 534;
            err_msg = "Data connection auth error";
            break;
            
          default:
                if (globus_l_gsc_is_ftp_code(response_type))
                {
                    err_code = response_type;
                    err_msg = strdup(response_msg);
                    response_msg = NULL;
                }
                else
                {
                    err_code = 500;
                    err_msg = "Command failed.\r\n";
                }
            break;
        }
        goto err;
    }
    else if(addr_count > wrapper->max && wrapper->max != -1)
    {
        err_code = 500;
        err_msg = "Command failed.\r\n";
        goto err;
    }
    else if(wrapper->dc_parsing_alg == 0)
    {
        /* if pasv */
        if(wrapper->cmd_ndx == 1)
        {
            if(globus_libc_contact_string_to_ints(
                cs[0], host_ip, &ip_count, &port) != GLOBUS_SUCCESS)
            {
                err_code = 500; 
                err_msg = "Resource error.\r\n";
                goto err;
            }
            if(ip_count > 4)
            {
                err_code = 522;
                err_msg = "Network protocol not supported.\r\n";
                goto err;
            }
            
            msg = globus_common_create_string(
                "%d Entering Passive Mode (%d,%d,%d,%d,%d,%d)\r\n",
                    wrapper->reply_code,
                    host_ip[0],
                    host_ip[1],
                    host_ip[2],
                    host_ip[3],
                    (int) (port / 256),
                    (int) (port % 256));
        }
        else
        {
            /* allow SPAS to work until real striping gets done */
            if(addr_count == -1)
            {
                addr_count = 1;
            }
            msg =  globus_common_create_string(
                "%d-Entering Striped Passive Mode.\r\n", 
                wrapper->reply_code);
            for(ctr = 0; ctr < addr_count; ctr++)
            {
                if(globus_libc_contact_string_to_ints(
                    cs[ctr], host_ip, &ip_count, &port) != GLOBUS_SUCCESS)
                {
                    err_code = 500;
                    err_msg = "Resource problem.\r\n";
                    goto err;
                }
                if(ip_count > 4)
                {
                    err_code = 522;
                    err_msg = "Network protocol not supported.\r\n";
                    goto err;
                }
            
                tmp_ptr = globus_common_create_string(
                    "%s %d,%d,%d,%d,%d,%d\r\n",
                    msg,
                    host_ip[0],
                    host_ip[1],
                    host_ip[2],
                    host_ip[3],
                    (int) (port / 256),
                    (int) (port % 256));
                if(tmp_ptr == NULL)
                {
                    err_code = 500;
                    err_msg = "Resource error.\r\n";
                    goto err;
                }
                globus_free(msg);
                msg = tmp_ptr;
            }
            tmp_ptr = globus_common_create_string("%s%d End\r\n", 
                msg, wrapper->reply_code);
            if(tmp_ptr == NULL)
            {
                err_code = 500;
                err_msg = "Resource error.\r\n";
                goto err;
            }
            globus_free(msg);
            msg = tmp_ptr;
        }
    }
    else if(wrapper->dc_parsing_alg == 1)
    {
        /* if epsv */
        if(wrapper->cmd_ndx == 2)
        {
            char *                      p;
            char *                      h;
            
            host = globus_libc_strdup(cs[0]);
            if(!host)
            {
                err_code = 500;
                err_msg = "Resource error.\r\n";
                goto err;
            }
            
            p = strrchr(host, ':');
            if(!p || p == host)
            {
                globus_free(host);
                err_code = 500;
                err_msg = "Internal Parse error.\r\n";
                goto err;
            }
            
            h = host;
            if(*host == '[')
            {
                h++;
                *(p - 1) = 0;
                if (strncmp(h, "::ffff:", 7) == 0) /* IPv4 mapped address */
                {
                    h += 7;
                    *host = ' ';
                }
            }
            else
            {
                *p = 0;
            }
            p++;
            
            sc = sscanf(p, "%hu", &port);
            if(sc != 1)
            {
                globus_free(host);
                err_code = 500;
                err_msg = "Internal Parse error.\r\n";
                goto err;
            }
            
            if(op->server_handle->epsv_ip)
            {
                msg = globus_common_create_string(
                    "%d Entering Passive Mode (|%d|%s|%d|)\r\n",
                        wrapper->reply_code,
                        *host == '[' ? 2 : 1,
                        h,
                        (int) port);
            }
            else
            {
                msg = globus_common_create_string(
                    "%d Entering Passive Mode (|||%d|)\r\n",
                        wrapper->reply_code,                    
                        (int) port);
            }
            globus_free(host);
        }
        else
        {
            /* allow SPAS to work until real striping gets done */
            if(addr_count == -1)
            {
                addr_count = 1;
            }
            msg =  globus_common_create_string(
                "%d-Entering Striped Passive Mode.\r\n", wrapper->reply_code);
            for(ctr = 0; ctr < addr_count; ctr++)
            {
                char *                  p;
                char *                  h;
                
                host = globus_libc_strdup(cs[ctr]);
                if(!host)
                {
                    err_code = 500;
                    err_msg = "Resource error.\r\n";
                    goto err;
                }
                
                p = strrchr(host, ':');
                if(!p || p == host)
                {
                    globus_free(host);
                    err_code = 500;
                    err_msg = "Internal Parse error.\r\n";
                    goto err;
                }
                
                h = host;
                if(*cs[0] == '[')
                {
                    h++;
                    *(p - 1) = 0;
                }
                else
                {
                    *p = 0;
                }
                p++;
            
                sc = sscanf(p, "%hu", &port);
                if(sc != 1)
                {
                    globus_free(host);
                    err_code = 500;
                    err_msg = "Internal Parse error.\r\n";
                    goto err;
                }
                
                tmp_ptr = globus_common_create_string(
                    "%s |%d|%s|%d|\r\n",
                    msg,
                    *cs[ctr] == '[' ? 2 : 1,
                    h,
                    (int) port);
                globus_free(host);
                if(tmp_ptr == NULL)
                {
                    err_code = 500;
                    err_msg = "Resource error.\r\n";
                    goto err;
                }
                globus_free(msg);
                msg = tmp_ptr;
            }
            tmp_ptr = globus_common_create_string("%s%d End\r\n", 
                msg, wrapper->reply_code);
            if(tmp_ptr == NULL)
            {
                err_code = 500;
                err_msg = "Resource error.\r\n";
                goto err;
            }
            globus_free(msg);
            msg = tmp_ptr;
        }
    }
    else
    {
        globus_assert(GLOBUS_FALSE);
    }

    /* if we were in delayed passive mode we start transfer now */
    if(wrapper->transfer_flag)
    {
        globus_i_gsc_cmd_intermediate_reply(op, msg);
        globus_l_gsc_cmd_transfer(wrapper->transfer_info);
        op->server_handle->pasv_info = NULL;
        globus_free(wrapper);
        globus_free(msg);
    }
    else
    {
        globus_gsc_959_finished_command(op, msg);
        globus_free(msg);
        globus_free(wrapper);
    }

    return;

  err:

    if(msg != NULL)
    {
        globus_free(msg);
        msg = NULL;
    }
    if(response_msg != NULL)
    {
        msg = globus_common_create_string("%s : %s", err_msg, response_msg);
    }
    else
    {
        msg = err_msg;
    }
    op->server_handle->pasv_info = NULL;
    tmp_ptr = globus_gsc_string_to_959(err_code, msg, NULL);
    globus_gsc_959_finished_command(op, tmp_ptr);
    globus_free(tmp_ptr);
    if(response_msg != NULL)
    {
        globus_free(msg);
    }
    globus_free(wrapper);
}

/*
 *  passive
 */
static void
globus_l_gsc_cmd_pasv(
    globus_i_gsc_op_t *                     op,
    const char *                            full_command,
    char **                                 cmd_a,
    int                                     argc,
    void *                                  user_arg)
{
    int                                     sc;
    globus_i_gsc_cmd_wrapper_t *            wrapper = NULL;
    char *                                  msg = NULL;
    globus_bool_t                           reply_flag;
    globus_result_t                         res;
    GlobusGridFTPServerName(globus_l_gsc_cmd_pasv);

    wrapper = (globus_i_gsc_cmd_wrapper_t *)
        globus_calloc(1, sizeof(globus_i_gsc_cmd_wrapper_t));

    globus_i_gsc_log(op->server_handle, full_command,
        GLOBUS_GRIDFTP_SERVER_CONTROL_LOG_TRANSFER_STATE);
    reply_flag = op->server_handle->opts.delayed_passive;

    if(strncmp(cmd_a[0], "PASV", 4) == 0)
    {
        wrapper->dc_parsing_alg = 0;
        wrapper->max = 1;
        wrapper->prt = GLOBUS_GRIDFTP_SERVER_CONTROL_PROTOCOL_IPV4;
        wrapper->cmd_ndx = 1;
        wrapper->reply_code = 227;
    }
    else if(strncmp(cmd_a[0], "EPSV", 4) == 0)
    {
        wrapper->dc_parsing_alg = 1;
        wrapper->prt = op->server_handle->ipv6 == GLOBUS_TRUE ?
                       GLOBUS_GRIDFTP_SERVER_CONTROL_PROTOCOL_IPV6 :
                       GLOBUS_GRIDFTP_SERVER_CONTROL_PROTOCOL_IPV4;
        wrapper->max = 1;
        if(argc == 2)
        {
            if(strstr(cmd_a[1], "ALL") != NULL)
            {
                reply_flag = GLOBUS_TRUE;
                op->server_handle->opts.passive_only = GLOBUS_TRUE;
                msg = _FSMSL("229 EPSV ALL Successful.\r\n");
            }
            else
            {
                sc = sscanf(cmd_a[1], "%d", (int*)&wrapper->prt);
                if(sc != 1)
                {
                    reply_flag = GLOBUS_TRUE;
                    msg = _FSMSL("501 Invalid network command.\r\n");
                }
                else if(wrapper->prt !=
                    GLOBUS_GRIDFTP_SERVER_CONTROL_PROTOCOL_IPV4
                    && wrapper->prt !=
                        GLOBUS_GRIDFTP_SERVER_CONTROL_PROTOCOL_IPV6)
                {
                    reply_flag = GLOBUS_TRUE;
                    msg = _FSMSL("501 Invalid protocol.\r\n");
                }
            }
        }
        
        wrapper->reply_code = 229;
        wrapper->cmd_ndx = 2;
    }
    else if(strcmp(cmd_a[0], "SPAS") == 0)
    {
        wrapper->max = -1;
        wrapper->cmd_ndx = 3;
        wrapper->reply_code = 229;
        wrapper->prt = GLOBUS_GRIDFTP_SERVER_CONTROL_PROTOCOL_IPV4;
        
        /* optional 2nd argument similar to epsv */
        if(argc == 2)
        {
            wrapper->dc_parsing_alg = 1;
            if(*cmd_a[1] == '2')
            {
                wrapper->prt = GLOBUS_GRIDFTP_SERVER_CONTROL_PROTOCOL_IPV6;
            }
        }
        else
        {
            wrapper->dc_parsing_alg = 0;
        }
    }
    else
    {
        globus_assert(GLOBUS_FALSE);
    }

    /*
     *  if delayed just wait for it
     */
    if(!reply_flag)
    {
        res = globus_i_gsc_passive(
            op,
            wrapper->max,
            wrapper->prt,
            NULL,
            globus_l_gsc_cmd_pasv_cb,
            wrapper);
        if(res != GLOBUS_SUCCESS)
        {
            globus_gsc_959_finished_command(op, _FSMSL("500 command failed.\r\n"));
        }
    }
    else
    {
        wrapper->transfer_flag = GLOBUS_TRUE;
        wrapper->reply_code -= 100;
        op->server_handle->pasv_info = wrapper;
        if(msg == NULL)
        {
            msg = _FSMSL("200 Passive delayed.\r\n");
        }
        globus_gsc_959_finished_command(op, msg);
    }
}

/*
 *  port
 */
static void
globus_l_gsc_cmd_port_cb(
    globus_i_gsc_op_t *                     op,
    globus_gridftp_server_control_response_t response_type,
    char *                                  response_msg,
    void *                                  user_arg)
{
    int                                     i;
    globus_i_gsc_cmd_wrapper_t *            wrapper;
    int                                     code;
    char *                                  msg;
    char *                                  tmp_ptr;

    wrapper = (globus_i_gsc_cmd_wrapper_t *) user_arg;
    if(response_type != GLOBUS_GRIDFTP_SERVER_CONTROL_RESPONSE_SUCCESS)
    {
        switch(response_type)
        {
          case GLOBUS_GRIDFTP_SERVER_CONTROL_RESPONSE_DATA_CONN_AUTH:
            code = 534;
            msg = strdup("Data connection auth error");
            break;
            
          default:
            if (globus_l_gsc_is_ftp_code(response_type))
            {
                code = response_type;
                msg = strdup(response_msg);
                response_msg = NULL;
            }
            else
            {
                code = 500;
                msg = strdup(_FSMSL("PORT Command failed."));
            }
            break;
        }
    }
    else
    {
        /* if port is successful we know that we are not delaying the pasv */
        op->server_handle->opts.delayed_passive = GLOBUS_FALSE;
        code = 200;
        msg = strdup(_FSMSL("PORT Command successful."));
    }
    if(response_msg != NULL)
    {
        tmp_ptr = msg;
        msg = globus_common_create_string("%s : %s", msg, response_msg);
        free(tmp_ptr);
    }
    tmp_ptr = globus_gsc_string_to_959(code, msg, NULL);
    globus_gsc_959_finished_command(op, tmp_ptr);
    globus_free(tmp_ptr);
    globus_free(msg);

    for(i = 0; i < wrapper->cs_count; i++)
    {
        globus_free(wrapper->cs[i]);
    }
    globus_free(wrapper->cs);
    globus_free(wrapper);
}

static void
globus_l_gsc_cmd_port(
    globus_i_gsc_op_t *                     op,
    const char *                            full_command,
    char **                                 cmd_a,
    int                                     argc,
    void *                                  user_arg)
{
    unsigned                                host_ip[4];
    unsigned                                hi;
    unsigned                                low;
    unsigned                                port;
    int                                     sc;
    int                                     i;
    int                                     stripe_count;
    char                                    delim;
    globus_i_gsc_cmd_wrapper_t *            wrapper = NULL;
    char *                                  msg = NULL;
    char *                                  p;
    char **                                 contact_strings = NULL;
    globus_result_t                         res;
    GlobusGridFTPServerName(globus_l_gsc_cmd_port);

    wrapper = (globus_i_gsc_cmd_wrapper_t *) globus_calloc(
        1, sizeof(globus_i_gsc_cmd_wrapper_t));
    if(wrapper == NULL)
    {
        goto err;
    }
    wrapper->op = op;

    globus_i_gsc_log(op->server_handle, full_command,
        GLOBUS_GRIDFTP_SERVER_CONTROL_LOG_TRANSFER_STATE);
    if(strcmp(cmd_a[0], "PORT") == 0)
    {
        wrapper->max = 1;
        stripe_count = 1;
    }
    else if(strcmp(cmd_a[0], "SPOR") == 0)
    {
        wrapper->max = -1;
        
        stripe_count = 1; /* for the first */
        p = cmd_a[1];
        while((p = strchr(p, ' ')))
        {
            stripe_count++;
            p++;
        }
    }
    else if(strcmp(cmd_a[0], "EPRT") == 0)
    {
        wrapper->max = 1;
        stripe_count = 1;
    }
    else
    {
        globus_assert(GLOBUS_FALSE);
    }
    
    contact_strings = globus_libc_calloc(stripe_count, sizeof(char *));
    if(contact_strings == NULL)
    {
        goto err;
    }
            
    /* move to the first command argument */
    p = cmd_a[1];
    while(isspace(*p)) p++;
    if(isdigit(delim = *p))
    {
        delim = 0;
    }
    
    for(i = 0; i < stripe_count && *p; i++)
    {
        if(delim)
        {
            /* |prt|ip|port| */
            while(*p && *p != delim) p++;
            if(*p)
            {
                p++;
                if(*p != delim)
                {
                    /* get prt portion */
                    if(sscanf(p, "%d", &sc) < 1 || (sc != 1 && sc != 2))
                    {
                        msg = 
                          "522 Network protocol not supported, use (1,2).\r\n";
                        break;
                    }
                    
                    /* may need to handle mixed prt in striped mode */
                    if(sc == 1)
                    {
                        wrapper->prt =
                            GLOBUS_GRIDFTP_SERVER_CONTROL_PROTOCOL_IPV4;
                    }
                    else
                    {
                        wrapper->prt =
                            GLOBUS_GRIDFTP_SERVER_CONTROL_PROTOCOL_IPV6;
                    }
                    
                    while(*p && *p != delim) p++;
                }
                else
                {
                    msg = "501 Malformed argument.\r\n";
                    break;
                }
            }
            
            if(*p)
            {
                int                     j = 0;
                char *                  s;
                char *                  c;
                char                    buf[100];
                
                p++;
                c = strchr(p, ':');
                s = strchr(p, delim);
                
                if(*p != delim)
                {
                    /* get ip portion */
                    if(c && c < s)
                    {
                        buf[j++] = '[';
                    }
                
                    while(j < sizeof(buf) - 1 && p < s)
                    {
                        buf[j++] = *(p++);
                    }
                    
                    /* need room for ], :, 5 digits, and nul */
                    if(*p == delim && j + 7 < sizeof(buf))
                    {
                        /* get port portion */
                        p++;
                        if(*buf == '[')
                        {
                            buf[j++] = ']';
                        }
                        
                        buf[j++] = ':';
                        while(j < sizeof(buf) - 1 &&
                            isdigit(*p) && *p != delim)
                        {
                            buf[j++] = *(p++);
                        }
                        if(*p == delim)
                        {
                            p++;
                        }
                        else
                        {
                            msg = "501 Malformed argument.\r\n";
                            break;
                        }
                        
                        buf[j] = 0;
                        
                        contact_strings[i] = globus_libc_strdup(buf);
                    }
                    else
                    {
                        msg = "501 Malformed argument.\r\n";
                        break;
                    }
                }
                else
                {
                    msg = "501 Malformed argument.\r\n";
                    break;
                }
            }
        }
        else
        {
            int                         consumed;
            
            wrapper->prt = GLOBUS_GRIDFTP_SERVER_CONTROL_PROTOCOL_IPV4;
            
            while(*p && !isdigit(*p)) p++;
    
            sc = sscanf(p,
                        "%u,%u,%u,%u,%u,%u%n",
                        &host_ip[0],
                        &host_ip[1],
                        &host_ip[2],
                        &host_ip[3],
                        &hi,
                        &low,
                        &consumed);
            port = hi * 256 + low;
            /* if string improperly parsed */
            if(sc < 6)
            {
                msg = _FSMSL("501 Illegal PORT command.\r\n");
                break;
            }
            /* if received port is not valid */
            else if(host_ip[0] > 255 ||
                    host_ip[1] > 255 ||
                    host_ip[2] > 255 ||
                    host_ip[3] > 255 ||
                    port > 65535)
            {
                msg = _FSMSL("501 Illegal PORT command.\r\n");
                break;
            }
            /* all is well with the client string */
            else
            {
                /* create the stripe count string */
                contact_strings[i] = globus_common_create_string(
                    "%u.%u.%u.%u:%d",
                    host_ip[0], host_ip[1], host_ip[2], host_ip[3], port);
                p += consumed;
            }
        }
        
        if(!contact_strings[i])
        {
            msg = _FSMSL("501 Resource error.\r\n");
            break;
        }
    }

    if(i < stripe_count && msg == NULL)
    {
        msg = _FSMSL("501 Illegal PORT command.\r\n");
    }
    if(msg != NULL)
    {
        globus_gsc_959_finished_command(op, msg);
        globus_free(wrapper);
        globus_free(contact_strings);
    }
    else
    {
        wrapper->cs = contact_strings;
        wrapper->cs_count = stripe_count;
        res = globus_i_gsc_port(
                op,
                (const char **)contact_strings,
                stripe_count,
                wrapper->prt,
                globus_l_gsc_cmd_port_cb,
                wrapper);
        if(res != GLOBUS_SUCCESS)
        {
            goto err;
        }
    }

    return;

  err:
    if(contact_strings != NULL)
    {
        globus_free(contact_strings);
    }
    if(wrapper != NULL)
    {
        globus_free(wrapper);
    }
    globus_i_gsc_command_panic(op);
}

/*************************************************************************
 *                          transfer functions
 *                          ------------------
 ************************************************************************/

static void 
globus_l_gsc_data_cb(
    globus_i_gsc_op_t *                     op,
    globus_gridftp_server_control_response_t response_type,
    char *                                  response_msg,
    void *                                  user_arg)
{
    int                                     code;
    char *                                  msg;
    char *                                  tmp_ptr;
    globus_i_gsc_cmd_wrapper_t *            wrapper;

    wrapper = (globus_i_gsc_cmd_wrapper_t *) user_arg;

    if(response_type != GLOBUS_GRIDFTP_SERVER_CONTROL_RESPONSE_SUCCESS)
    {
        if (globus_l_gsc_is_ftp_code(response_type))
        {
            code = response_type;
            msg = strdup(response_msg);
            response_msg = NULL;
        }
        else
        {
            code = 500;
            msg = strdup(_FSMSL("Command failed."));
        }
    }
    else
    {
        code = 226;
        msg = strdup(_FSMSL("Transfer Complete."));
    }
    if(response_msg != NULL)
    {
        tmp_ptr = msg;
        msg = globus_common_create_string("%s : %s", msg, response_msg);
        free(tmp_ptr);
    }
    tmp_ptr = globus_gsc_string_to_959(code, msg, NULL);
    globus_gsc_959_finished_command(wrapper->op, tmp_ptr);
    globus_free(tmp_ptr);
    globus_free(msg);

    if(wrapper->mod_name)
    {
        globus_free(wrapper->mod_name);
    }
    if(wrapper->mod_parms)
    {
        globus_free(wrapper->mod_parms);
    }
    if(wrapper->path)
    {
        globus_free(wrapper->path);
    }
    globus_free(wrapper);
}

static void
globus_l_gsc_cmd_transfer(
    globus_i_gsc_cmd_wrapper_t *            wrapper)
{
    globus_result_t                         res;

    switch(wrapper->type)
    {
        case GLOBUS_L_GSC_OP_TYPE_SEND:
            res = globus_i_gsc_send(
                wrapper->op,
                wrapper->path,
                wrapper->mod_name,
                wrapper->mod_parms,
                globus_l_gsc_data_cb,
                wrapper);
            break;

        case GLOBUS_L_GSC_OP_TYPE_RECV:
            res = globus_i_gsc_recv(
                wrapper->op,
                wrapper->path,
                wrapper->mod_name,
                wrapper->mod_parms,
                globus_l_gsc_data_cb,
                wrapper);
            break;

        case GLOBUS_L_GSC_OP_TYPE_NLST:
        case GLOBUS_L_GSC_OP_TYPE_LIST:
        case GLOBUS_L_GSC_OP_TYPE_MLSD:
        case GLOBUS_L_GSC_OP_TYPE_MLSR:
            res = globus_i_gsc_list(
                wrapper->op,
                wrapper->path,
                GLOBUS_GRIDFTP_SERVER_CONTROL_RESOURCE_USER_DEFINED,
                wrapper->type,
                globus_l_gsc_data_cb,
                wrapper);
            break;
	    
        default:
            globus_assert(GLOBUS_FALSE);
            break;
    }

    if(res != GLOBUS_SUCCESS)
    {
        globus_gsc_959_finished_command(
            wrapper->op, _FSMSL("500 Command failed\r\n"));
        if(wrapper->mod_name)
        {
            globus_free(wrapper->mod_name);
        }
        if(wrapper->mod_parms)
        {
            globus_free(wrapper->mod_parms);
        }
        if(wrapper->path)
        {
            globus_free(wrapper->path);
        }
        globus_free(wrapper);
    }
}

/*
 *  stor/retr when we needed a size (APPE)
 */
static void
globus_l_gsc_cmd_stor_retr_cb(
    globus_i_gsc_op_t *                     op,
    globus_gridftp_server_control_response_t response_type,
    char *                                  response_msg,
    char *                                  path,
    char *                                  abs_path,
    globus_gridftp_server_control_stat_t *  stat_info,
    int                                     stat_count,
    uid_t                                   uid,
    void *                                  user_arg)
{
    int                                     code;
    char *                                  tmp_ptr;
    char *                                  msg = NULL;
    globus_i_gsc_cmd_wrapper_t *            wrapper = NULL;
    GlobusGridFTPServerName(globus_l_gsc_cmd_stor_retr_cb);

    wrapper = (globus_i_gsc_cmd_wrapper_t *) user_arg;
    wrapper->op = op;
    wrapper->path = globus_libc_strdup(path);    

    if(response_type == GLOBUS_GRIDFTP_SERVER_CONTROL_RESPONSE_SUCCESS &&
        stat_count > 1)
    {
        code = 550;
        msg = globus_common_create_string(
            _FSMSL("Path is a directory."));
    }
       
    if(response_type != GLOBUS_GRIDFTP_SERVER_CONTROL_RESPONSE_SUCCESS ||
        stat_count < 1)
    {
        /* we'll mimic a STOR here since the file doesn't exist */
        wrapper->mod_name = NULL;
        wrapper->mod_parms = NULL;        
    }
    else
    {
        /* for an append we mimic an ESTO A */
        wrapper->mod_name = globus_libc_strdup("A");
        wrapper->mod_parms = globus_common_create_string(
            "%"GLOBUS_OFF_T_FORMAT, stat_info->size);
    }
    
    if(msg != NULL)
    {
        tmp_ptr = globus_gsc_string_to_959(code, msg, NULL);
        globus_gsc_959_finished_command(op, tmp_ptr);
        globus_free(tmp_ptr);
        globus_free(msg);
        if(wrapper->mod_name)
        {
            globus_free(wrapper->mod_name);
        }
        if(wrapper->mod_parms)
        {
            globus_free(wrapper->mod_parms);
        }
        if(wrapper->path)
        {
            globus_free(wrapper->path);
        }
        globus_free(wrapper);
    }
    else
    {
        /* clean up these here since we are going to reuse this op and
            they will get leaked  --- should probably fix at leak time */
        if(op->response_msg != NULL)
        {
            globus_free(op->response_msg);
            op->response_msg = NULL;
        }
        if(op->path != NULL)
        {
            globus_free(op->path);
            op->path = NULL;
        }
        globus_l_gsc_cmd_transfer(wrapper);
    }
    return;
    
}

/*
 *  stor
 */
static void
globus_l_gsc_cmd_stor_retr(
    globus_i_gsc_op_t *                     op,
    const char *                            full_command,
    char **                                 cmd_a,
    int                                     argc,
    void *                                  user_arg)
{
    char *                                  arg2;
    int                                     sc;
    globus_result_t                         res;
    char *                                  path = NULL;
    char *                                  mod_name = NULL;
    char *                                  mod_parm = NULL;
    char *                                  tmp_ptr = NULL;
    globus_i_gsc_cmd_wrapper_t *            wrapper = NULL;
    globus_off_t                            tmp_o;
    globus_bool_t                           transfer = GLOBUS_TRUE;
    GlobusGridFTPServerName(globus_l_gsc_cmd_stor);

    if(argc != 1 && argc != 2)
    {
        globus_gsc_959_finished_command(op, _FSMSL("500 command failed.\r\n"));
        return;
    }
    wrapper = (globus_i_gsc_cmd_wrapper_t *) globus_malloc(
        sizeof(globus_i_gsc_cmd_wrapper_t));
    if(wrapper == NULL)
    {
        globus_i_gsc_command_panic(op);
        return;
    }
    wrapper->op = op;

    globus_i_gsc_log(op->server_handle, full_command,
        GLOBUS_GRIDFTP_SERVER_CONTROL_LOG_TRANSFER);
    if(strcmp(cmd_a[0], "STOR") == 0 ||
        strcmp(cmd_a[0], "ESTO") == 0 ||
        strcmp(cmd_a[0], "APPE") == 0)
    {
        wrapper->type = GLOBUS_L_GSC_OP_TYPE_RECV;
    }
    else if(strcmp(cmd_a[0], "RETR") == 0 ||  strcmp(cmd_a[0], "ERET") == 0)
    {
        wrapper->type = GLOBUS_L_GSC_OP_TYPE_SEND;
    }
    else if(strcmp(cmd_a[0], "LIST") == 0)
    {
        wrapper->type = GLOBUS_L_GSC_OP_TYPE_LIST;
    }
    else if(strcmp(cmd_a[0], "NLST") == 0)
    {
        wrapper->type = GLOBUS_L_GSC_OP_TYPE_NLST;
    }
    else if(strcmp(cmd_a[0], "MLSD") == 0)
    {
        wrapper->type = GLOBUS_L_GSC_OP_TYPE_MLSD;
    }
    else if(strcmp(cmd_a[0], "MLSR") == 0)
    {
        wrapper->type = GLOBUS_L_GSC_OP_TYPE_MLSR;
    }
    else
    {
        globus_assert(0 && "func shouldn't be called for this command");
    }

    if(strcmp(cmd_a[0], "STOR") == 0 ||
            strcmp(cmd_a[0], "RETR") == 0)
    {
        path = globus_libc_strdup(cmd_a[1]);
        mod_name = NULL;
        mod_parm = NULL;
    }
    else if(strcmp(cmd_a[0], "APPE") == 0)
    {
        int                             mask = 
            GLOBUS_GRIDFTP_SERVER_CONTROL_RESOURCE_FILE_ONLY;

        res = globus_i_gsc_resource_query(
                op,
                cmd_a[1],
                mask,
                globus_l_gsc_cmd_stor_retr_cb,
                wrapper);
        if(res != GLOBUS_SUCCESS)
        {
            globus_free(wrapper);
            globus_gsc_959_finished_command(
                op, _FSMSL("500 Command not supported.\r\n"));
            return;
        }
        transfer = GLOBUS_FALSE;
    }
    else if(strcmp(cmd_a[0], "ESTO") == 0 ||
        strcmp(cmd_a[0], "ERET") == 0)
    {
        if(strncasecmp(cmd_a[1], "P ", 2) == 0 && strcmp(cmd_a[0], "ERET") == 0)
        {
            arg2 = cmd_a[1] + 2;
            while(isspace(*arg2) && *arg2 != '\0') arg2++;

            sc = sscanf(arg2, 
                "%"GLOBUS_OFF_T_FORMAT" %"GLOBUS_OFF_T_FORMAT, 
                &tmp_o, &tmp_o);
            if(sc != 2)
            {
                globus_free(wrapper);
                globus_gsc_959_finished_command(
                    op, _FSMSL("500 command failed.\r\n"));
                return;
            }
            mod_parm = globus_libc_strdup(arg2);
            tmp_ptr = mod_parm;
            while(isdigit(*tmp_ptr)) tmp_ptr++;
            while(isspace(*tmp_ptr)) tmp_ptr++;
            while(isdigit(*tmp_ptr)) tmp_ptr++;
            /* up until here the scanf gauentess safety */
            while(isspace(*tmp_ptr) && *tmp_ptr != '\0') tmp_ptr++;
            if(*tmp_ptr == '\0')
            {
                globus_free(mod_parm);
                globus_free(wrapper);
                globus_gsc_959_finished_command(op, _FSMSL("501 bad parameter.\r\n"));
                return;
            }
            *(tmp_ptr-1) = '\0';

            path = globus_libc_strdup(tmp_ptr);
            mod_name = globus_libc_strdup("P");
        }
        else if(strncasecmp(cmd_a[1], "A ", 2) == 0 &&
            strcmp(cmd_a[0], "ESTO") == 0)
        {
            arg2 = cmd_a[1] + 2;
            while(isspace(*arg2) && *arg2 != '\0') arg2++;
            sc = sscanf(arg2, "%"GLOBUS_OFF_T_FORMAT, &tmp_o);
            if(sc != 1)
            {
                globus_free(wrapper);
                globus_gsc_959_finished_command(op, _FSMSL("501 bad parameter.\r\n"));
                return;
            }
            mod_parm = globus_libc_strdup(arg2);
            tmp_ptr = mod_parm;
            while(isdigit(*tmp_ptr)) tmp_ptr++;
            /* up until here the scanf gauentess safety */
            while(isspace(*tmp_ptr) && *tmp_ptr != '\0') tmp_ptr++;
            if(*tmp_ptr == '\0')
            {
                globus_free(mod_parm);
                globus_free(wrapper);
                globus_gsc_959_finished_command(op, _FSMSL("501 bad parameter.\r\n"));
                return;
            }
            *(tmp_ptr-1) = '\0';

            path = globus_libc_strdup(tmp_ptr);
            mod_name = globus_libc_strdup("A");
        }
        else
        {
            mod_name = globus_libc_strdup(cmd_a[1]);
            if(mod_name == NULL)
            {
                globus_free(wrapper);
                globus_i_gsc_command_panic(op);
                return;
            }

            tmp_ptr = strstr(mod_name, "=\"");
            if(tmp_ptr == NULL)
            {
                globus_free(mod_name);
                globus_free(wrapper);
                globus_gsc_959_finished_command(op, _FSMSL("500 command failed.\r\n"));
                return;
            }

            *tmp_ptr = '\0';
            tmp_ptr += 2;
            mod_parm = globus_libc_strdup(tmp_ptr);
            tmp_ptr = strchr(mod_parm, '\"');
            /* XXX if mod_parm has a space, it has already been misparsed...
               need to fix this upstream, but just error out for now */
            if(tmp_ptr == NULL)
            {
                globus_free(mod_name);
                globus_free(mod_parm);
                globus_free(wrapper);
                globus_gsc_959_finished_command(op, _FSMSL("500 command failed: space.\r\n"));
                return;
            }            
            *tmp_ptr = '\0';
            if(tmp_ptr[1] == '\0' || tmp_ptr[2] == '\0')
            {
                globus_free(mod_name);
                globus_free(mod_parm);
                globus_free(wrapper);
                globus_gsc_959_finished_command(
                    op, _FSMSL("500 command failed: space.\r\n"));
                return;
            }
            tmp_ptr += 2;

            path = globus_libc_strdup(tmp_ptr);
        }
    }
    /* all list stuff is here */
    else
    {
        if(cmd_a[1] == NULL)
        {
            path = strdup(op->server_handle->cwd);
        }
        else
        {   tmp_ptr = cmd_a[1];
            /* ignore non-standard list arguments */
            if(wrapper->type == GLOBUS_L_GSC_OP_TYPE_LIST)
            {
                while(*tmp_ptr == '-')
                {
                    tmp_ptr++;
                    while(*tmp_ptr != ' ' && *tmp_ptr != '\0')
                    {
                        tmp_ptr++;
                    }
                    while(*tmp_ptr == ' ')
                    {
                        tmp_ptr++;
                    }
                }
                
                if((*tmp_ptr == '*' && *(tmp_ptr + 1) == '\0') || 
                    *tmp_ptr == '\0')
                {
                    path = globus_libc_strdup(op->server_handle->cwd);
                }
                else
                {
                    int                 len;
                    char *              fn_ptr;
                    
                    len = strlen(tmp_ptr);
                    if(strcspn(tmp_ptr, "[]*?") == len)
                    {
                        path = globus_libc_strdup(tmp_ptr);
                    }
                    else
                    {   
                        if((fn_ptr = strrchr(tmp_ptr, '/')) != NULL)
                        {
                            fn_ptr++;
                            path = globus_libc_strdup(tmp_ptr);
                            *(path + (fn_ptr - tmp_ptr)) = '\0';
                            op->glob_match_str = globus_libc_strdup(fn_ptr);
                        }
                        else
                        {
                            op->glob_match_str = globus_libc_strdup(tmp_ptr);
                            path = globus_libc_strdup(op->server_handle->cwd);
                        }
                    }
                }
            }
            else
            {
                path = strdup(tmp_ptr);
            }
        }
    }

    if(transfer)
    {
        wrapper->mod_name = mod_name;
        wrapper->mod_parms = mod_parm;
        wrapper->path = path;
        wrapper->reply_code = 129;
        /* if in delayed passive tell library to go passive */
        if(op->server_handle->opts.delayed_passive &&
            op->server_handle->pasv_info != NULL)
        {
            op->server_handle->pasv_info->transfer_info = wrapper;
            res = globus_i_gsc_passive(
                wrapper->op,
                op->server_handle->pasv_info->max,
                op->server_handle->pasv_info->prt,
                wrapper->path,
                globus_l_gsc_cmd_pasv_cb,
                op->server_handle->pasv_info);
            if(res != GLOBUS_SUCCESS)
            {
                globus_free(wrapper);
                globus_gsc_959_finished_command(op, _FSMSL("500 command failed.\r\n"));
            }
        }
        else
        {
            globus_l_gsc_cmd_transfer(wrapper);
        }
    }

    return;
}

/*************************************************************************
 *                          helpers
 *                          -------
 ************************************************************************/

void
globus_i_gsc_add_commands(
    globus_i_gsc_server_handle_t *          server_handle)
{
    globus_gsc_959_command_add(
        server_handle,
        NULL,
        globus_l_gsc_cmd_all,
        GLOBUS_GSC_COMMAND_PRE_AUTH |
            GLOBUS_GSC_COMMAND_POST_AUTH,
        1,
        1,
        "",
        NULL);

    globus_gsc_959_command_add(
        server_handle,
        "ALLO", 
        globus_l_gsc_cmd_allo,
        GLOBUS_GSC_COMMAND_POST_AUTH,
        2,
        2,
        "ALLO <sp> <size>",
        NULL);

    globus_gsc_959_command_add(
        server_handle,
        "APPE", 
        globus_l_gsc_cmd_stor_retr,
        GLOBUS_GSC_COMMAND_POST_AUTH,
        2,
        2,
        "APPE <sp> pathname",
        NULL);

    globus_gsc_959_command_add(
        server_handle,
        "CWD", 
        globus_l_gsc_cmd_cwd,
        GLOBUS_GSC_COMMAND_POST_AUTH,
        2,
        2,
        "CWD <sp> pathname",
        NULL);

    globus_gsc_959_command_add(
        server_handle,
        "CDUP", 
        globus_l_gsc_cmd_cwd,
        GLOBUS_GSC_COMMAND_POST_AUTH,
        1,
        1,
        "CDUP (up one directory)",
        NULL);

    globus_gsc_959_command_add(
        server_handle,
        "DCAU", 
        globus_l_gsc_cmd_dcau,
        GLOBUS_GSC_COMMAND_POST_AUTH,
        2,
        3,
        "DCAU <S,N,A>",
        NULL);

    globus_gsc_959_command_add(
        server_handle,
        "EPSV", 
        globus_l_gsc_cmd_pasv,
        GLOBUS_GSC_COMMAND_POST_AUTH,
        1,
        2,
        "EPSV [<sp> ALL]",
        NULL);

    globus_gsc_959_command_add(
        server_handle,
        "ERET", 
        globus_l_gsc_cmd_stor_retr,
        GLOBUS_GSC_COMMAND_POST_AUTH,
        2,
        2,
        "ERET <sp> mod_name=\"mod_parms\" <sp> pathname",
        NULL);

    globus_gsc_959_command_add(
        server_handle,
        "ESTO", 
        globus_l_gsc_cmd_stor_retr,
        GLOBUS_GSC_COMMAND_POST_AUTH,
        2,
        2,
        "ESTO <sp> mod_name=\"mod_parms\" <sp> pathname",
        NULL);

    globus_gsc_959_command_add(
        server_handle,
        "FEAT", 
        globus_l_gsc_cmd_feat,
        GLOBUS_GSC_COMMAND_PRE_AUTH | 
            GLOBUS_GSC_COMMAND_POST_AUTH,
        1,
        1,
        "FEAT",
        NULL);

    globus_gsc_959_command_add(
        server_handle,
        "HELP", 
        globus_l_gsc_cmd_help,
        GLOBUS_GSC_COMMAND_PRE_AUTH | 
            GLOBUS_GSC_COMMAND_POST_AUTH,
        1,
        2,
        "HELP [<sp> command]",
        NULL);

    globus_gsc_959_command_add(
        server_handle,
        "LIST", 
        globus_l_gsc_cmd_stor_retr,
        GLOBUS_GSC_COMMAND_POST_AUTH,
        1,
        2,
        "LIST [<sp> <filename>]",
        NULL);

    globus_gsc_959_command_add(
        server_handle,
        "MDTM", 
        globus_l_gsc_cmd_mdtm,
        GLOBUS_GSC_COMMAND_POST_AUTH,
        2,
        2,
        "MDTM <sp> <filename>",
        NULL);

    globus_gsc_959_command_add(
        server_handle,
        "MODE", 
        globus_l_gsc_cmd_mode,
        GLOBUS_GSC_COMMAND_POST_AUTH,
        2,
        2,
        "MODE <sp> mode-code",
        NULL);

    globus_gsc_959_command_add(
        server_handle,
        "NLST", 
        globus_l_gsc_cmd_stor_retr,
        GLOBUS_GSC_COMMAND_POST_AUTH,
        1,
        2,
        "NLST [<sp> <filename>]",
        NULL);

    globus_gsc_959_command_add(
        server_handle,
        "MLSC",
        globus_l_gsc_cmd_stat,
        GLOBUS_GSC_COMMAND_POST_AUTH,
        1,
        2,
        "MLSC [<sp> <filename>]",
        (void *) GLOBUS_L_GSC_OP_TYPE_MLSD);

    globus_gsc_959_command_add(
        server_handle,
        "MLSD",
        globus_l_gsc_cmd_stor_retr,
        GLOBUS_GSC_COMMAND_POST_AUTH,
        1,
        2,
        "MLSD [<sp> <filename>]",
        NULL);

    globus_gsc_959_command_add(
        server_handle,
        "MLSR",
        globus_l_gsc_cmd_stor_retr,
        GLOBUS_GSC_COMMAND_POST_AUTH,
        1,
        2,
        "MLSR [<sp> <filename>]",
        NULL);

    globus_gsc_959_command_add(
        server_handle,
        "MLST",
        globus_l_gsc_cmd_stat,
        GLOBUS_GSC_COMMAND_POST_AUTH,
        1,
        2,
        "MLST [<sp> <filename>]",
        (void *)1);

    globus_gsc_959_command_add(
        server_handle,
        "NOOP", 
        globus_l_gsc_cmd_noop,
        GLOBUS_GSC_COMMAND_PRE_AUTH | 
            GLOBUS_GSC_COMMAND_POST_AUTH,
        1,
        1,
        "NOOP (no operation)",
        NULL);

    globus_gsc_959_command_add(
        server_handle,
        "OPTS", 
        globus_l_gsc_cmd_opts,
        GLOBUS_GSC_COMMAND_POST_AUTH,
        2,
        3,
        "OPTS <sp> opt-type [parameters]",
        NULL);

    globus_gsc_959_command_add(
        server_handle,
        "PASS", 
        globus_l_gsc_cmd_pass,
        GLOBUS_GSC_COMMAND_PRE_AUTH,
        1,
        2,
        "PASS <sp> password",
        NULL);

    globus_gsc_959_command_add(
        server_handle,
        "PASV", 
        globus_l_gsc_cmd_pasv,
        GLOBUS_GSC_COMMAND_POST_AUTH,
        1,
        1,
        "PASV",
        NULL);

    globus_gsc_959_command_add(
        server_handle,
        "PBSZ", 
        globus_l_gsc_cmd_pbsz,
        GLOBUS_GSC_COMMAND_POST_AUTH,
        2,
        2,
        "PBSZ <sp> size",
        NULL);

    globus_gsc_959_command_add(
        server_handle,
        "PORT", 
        globus_l_gsc_cmd_port,
        GLOBUS_GSC_COMMAND_POST_AUTH,
        2,
        2,
        "PORT <port>",
        NULL);

    globus_gsc_959_command_add(
        server_handle,
        "PROT", 
        globus_l_gsc_cmd_prot,
        GLOBUS_GSC_COMMAND_POST_AUTH,
        2,
        2,
        "PROT <C|P|S>",
        NULL);

    globus_gsc_959_command_add(
        server_handle,
        "EPRT", 
        globus_l_gsc_cmd_port,
        GLOBUS_GSC_COMMAND_POST_AUTH,
        2,
        2,
        "EPRT <sp> <port>",
        NULL);

    globus_gsc_959_command_add(
        server_handle,
        "SPOR", 
        globus_l_gsc_cmd_port,
        GLOBUS_GSC_COMMAND_POST_AUTH,
        2,
        2,
        "SPOR <sp> <port list>",
        NULL);

    globus_gsc_959_command_add(
        server_handle,
        "TREV", 
        globus_l_gsc_cmd_trev,
        GLOBUS_GSC_COMMAND_POST_AUTH,
        3,
        3,
        "TREV <event name> <frequency> [info list]",
        NULL);

    globus_gsc_959_command_add(
        server_handle,
        "PWD", 
        globus_l_gsc_cmd_pwd,
        GLOBUS_GSC_COMMAND_POST_AUTH,
        1,
        1,
        "PWD (returns current working directory)",
        NULL);

    globus_gsc_959_command_add(
        server_handle,
        "QUIT", 
        globus_l_gsc_cmd_quit,
        GLOBUS_GSC_COMMAND_PRE_AUTH | 
            GLOBUS_GSC_COMMAND_POST_AUTH,
        1,
        1,
        "QUIT (close control connection)",
        NULL);

    globus_gsc_959_command_add(
        server_handle,
        "REST", 
        globus_l_gsc_cmd_rest,
        GLOBUS_GSC_COMMAND_POST_AUTH,
        2,
        2,
        "REST [<sp> restart marker]",
        NULL);

    globus_gsc_959_command_add(
        server_handle,
        "RETR", 
        globus_l_gsc_cmd_stor_retr,
        GLOBUS_GSC_COMMAND_POST_AUTH,
        2,
        2,
        "RETR [<sp> pathname]",
        NULL);

    globus_gsc_959_command_add(
        server_handle,
        "SBUF", 
        globus_l_gsc_cmd_sbuf,
        GLOBUS_GSC_COMMAND_POST_AUTH,
        2,
        2,
        "SBUF <sp> window-size",
        NULL);

    globus_gsc_959_command_add(
        server_handle,
        "SIZE", 
        globus_l_gsc_cmd_size,
        GLOBUS_GSC_COMMAND_POST_AUTH,
        2,
        2,
        "SIZE <sp> pathname",
        NULL);

    globus_gsc_959_command_add(
        server_handle,
        "SPAS", 
        globus_l_gsc_cmd_pasv,
        GLOBUS_GSC_COMMAND_POST_AUTH,
        1,
        2,
        "SPAS",
        NULL);

    globus_gsc_959_command_add(
        server_handle,
        "STAT", 
        globus_l_gsc_cmd_stat,
        GLOBUS_GSC_COMMAND_POST_AUTH,
        1,
        2,
        "STAT [<sp> pathname]",
        0);

    globus_gsc_959_command_add(
        server_handle,
        "STOR", 
        globus_l_gsc_cmd_stor_retr,
        GLOBUS_GSC_COMMAND_POST_AUTH,
        2,
        2,
        "STOR [<sp> pathname]",
        NULL);

    globus_gsc_959_command_add(
        server_handle,
        "STRU", 
        globus_l_gsc_cmd_stru,
        GLOBUS_GSC_COMMAND_POST_AUTH,
        2,
        2,
        "STRU (specify file structure)",
        NULL);

    globus_gsc_959_command_add(
        server_handle,
        "SYST", 
        globus_l_gsc_cmd_syst,
        GLOBUS_GSC_COMMAND_POST_AUTH,
        1,
        1,
        "SYST (returns system type)",
        NULL);

    globus_gsc_959_command_add(
        server_handle,
        "TYPE", 
        globus_l_gsc_cmd_type,
        GLOBUS_GSC_COMMAND_POST_AUTH,
        2,
        2,
        "TYPE <sp> type-code",
        NULL);

    globus_gsc_959_command_add(
        server_handle,
        "USER", 
        globus_l_gsc_cmd_user,
        GLOBUS_GSC_COMMAND_PRE_AUTH,
        2,
        2,
        "USER <sp> username",
        NULL);

    globus_gsc_959_command_add(
        server_handle,
        "SITE SBUF", 
        globus_l_gsc_cmd_site_sbuf,
        GLOBUS_GSC_COMMAND_POST_AUTH,
        3,
        3,
        "SITE SBUF: set send and receive buffers",
        NULL);

    globus_gsc_959_command_add(
        server_handle,
        "SITE RETRBUFSIZE", 
        globus_l_gsc_cmd_site_receive_buf,
        GLOBUS_GSC_COMMAND_POST_AUTH,
        3,
        3,
        "SITE RETRBUFSIZE: set receive buffers",
        NULL);

    globus_gsc_959_command_add(
        server_handle,
        "SITE RBUFSZ", 
        globus_l_gsc_cmd_site_receive_buf,
        GLOBUS_GSC_COMMAND_POST_AUTH,
        3,
        3,
        "SITE RBUFSZ: set receive buffers",
        NULL);

    globus_gsc_959_command_add(
        server_handle,
        "SITE RBUFSIZ", 
        globus_l_gsc_cmd_site_receive_buf,
        GLOBUS_GSC_COMMAND_POST_AUTH,
        3,
        3,
        "SITE RBUFSIZ: set receive buffers",
        NULL);

    globus_gsc_959_command_add(
        server_handle,
        "SITE STORBUFSIZE", 
        globus_l_gsc_cmd_site_send_buf,
        GLOBUS_GSC_COMMAND_POST_AUTH,
        3,
        3,
        "SITE STORBUFSIZE: set send buffers",
        NULL);

    globus_gsc_959_command_add(
        server_handle,
        "SITE SBUFSZ", 
        globus_l_gsc_cmd_site_send_buf,
        GLOBUS_GSC_COMMAND_POST_AUTH,
        3,
        3,
        "SITE SBUFSZ: set send buffers",
        NULL);

    globus_gsc_959_command_add(
        server_handle,
        "SITE SBUFSIZ", 
        globus_l_gsc_cmd_site_send_buf,
        GLOBUS_GSC_COMMAND_POST_AUTH,
        3,
        3,
        "SITE SBUFSIZ: set send buffers",
        NULL);

    globus_gsc_959_command_add(
        server_handle,
        "SITE HELP", 
        globus_l_gsc_cmd_help,
        GLOBUS_GSC_COMMAND_PRE_AUTH | 
            GLOBUS_GSC_COMMAND_POST_AUTH,
        2,
        3,
        "SITE HELP: help on server commands",
        NULL);

    globus_gsc_959_command_add(
        server_handle,
        "SITE FAULT", 
        globus_l_gsc_cmd_site_fault,
        GLOBUS_GSC_COMMAND_POST_AUTH,
        3,
        3,
        "SITE FAULT: force a failure on given command",
        NULL);

    globus_gsc_959_command_add(
        server_handle,
        "LANG", 
        globus_l_gsc_cmd_lang,
        GLOBUS_GSC_COMMAND_POST_AUTH,
        1,
        2,
        "LANG: set language for messages",
        NULL);
    
    /* add features */
    globus_gridftp_server_control_add_feature(server_handle, "PASV AllowDelayed;");
    globus_gridftp_server_control_add_feature(server_handle, "MDTM");
    globus_gridftp_server_control_add_feature(server_handle, "REST STREAM");
    globus_gridftp_server_control_add_feature(server_handle, "SPOR");
    globus_gridftp_server_control_add_feature(server_handle, "SPAS");
    globus_gridftp_server_control_add_feature(server_handle, "ESTO");
    globus_gridftp_server_control_add_feature(server_handle, "ERET");
    globus_gridftp_server_control_add_feature(server_handle, "MLST Type*;Size*;Modify*;Perm*;Charset;UNIX.mode*;UNIX.owner*;UNIX.uid*;UNIX.group*;UNIX.gid*;Unique*;UNIX.slink*;X.count;");    
    globus_gridftp_server_control_add_feature(server_handle, "SIZE");    
    globus_gridftp_server_control_add_feature(server_handle, "PARALLEL");    
    globus_gridftp_server_control_add_feature(server_handle, "DCAU");    
    globus_gridftp_server_control_add_feature(server_handle, "LANG EN");    
    globus_gridftp_server_control_add_feature(server_handle, "UTF8");
    globus_gridftp_server_control_add_feature(server_handle, "MLSC");
    globus_gridftp_server_control_add_feature(server_handle, "MLSR");
}
