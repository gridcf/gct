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

#include "globus_i_gridftp_server_control.h"

#define GLOBUS_GRIDFTP_SERVER_HASHTABLE_SIZE    256

globus_result_t
globus_gridftp_server_control_attr_init(
    globus_gridftp_server_control_attr_t *  in_attr)
{
    globus_i_gsc_attr_t *                   attr;
    globus_result_t                         res;
    GlobusGridFTPServerName(globus_gridftp_server_control_attr_init);

    GlobusGridFTPServerDebugEnter();

    if(in_attr == NULL)
    {
        res = GlobusGridFTPServerErrorParameter("in_attr");
        goto err;
    }

    attr = (globus_i_gsc_attr_t *) globus_calloc(
                1, sizeof(globus_i_gsc_attr_t));
    if(attr == NULL)
    {
        res = GlobusGridFTPServerControlErrorSystem();
        goto err;
    }

    globus_hashtable_init(
        &attr->funcs.send_cb_table,
        GLOBUS_GRIDFTP_SERVER_HASHTABLE_SIZE,
        globus_hashtable_string_hash,
        globus_hashtable_string_keyeq);

    globus_hashtable_init(
        &attr->funcs.recv_cb_table,
        GLOBUS_GRIDFTP_SERVER_HASHTABLE_SIZE,
        globus_hashtable_string_hash,
        globus_hashtable_string_keyeq);

    attr->funcs.resource_cb = NULL;
    attr->version_ctl = GLOBUS_GRIDFTP_VERSION_CTL;
    attr->modes = globus_libc_strdup("ES");
    attr->types = globus_libc_strdup("AI");
    attr->base_dir = globus_libc_strdup("/");

    *in_attr = attr;

    GlobusGridFTPServerDebugExit();

    return GLOBUS_SUCCESS;

  err:

    GlobusGridFTPServerDebugExitWithError();

    return res;
}

globus_result_t
globus_gridftp_server_control_attr_destroy(
    globus_gridftp_server_control_attr_t    in_attr)
{
    globus_i_gsc_module_func_t *            mod_func;
    globus_list_t *                         list;
    globus_i_gsc_attr_t *                   attr;
    globus_result_t                         res;
    GlobusGridFTPServerName(globus_gridftp_server_control_attr_destroy);

    GlobusGridFTPServerDebugEnter();

    if(in_attr == NULL)
    {
        res = GlobusGridFTPServerErrorParameter("in_attr");
        goto err;
    }

    attr = in_attr;

    if(attr->version_ctl != GLOBUS_GRIDFTP_VERSION_CTL)
    {
        res = GlobusGridFTPServerErrorParameter("in_attr");
        goto err;
    }

    if(attr->pre_auth_banner != NULL)
    {
        globus_free(attr->pre_auth_banner);
    }
    if(attr->post_auth_banner != NULL)
    {
        globus_free(attr->post_auth_banner);
    }

    globus_hashtable_to_list(&attr->funcs.recv_cb_table, &list);
    while(!globus_list_empty(list))
    {
        mod_func = (globus_i_gsc_module_func_t *)
            globus_list_remove(&list, list);
        globus_free(mod_func->key);
        globus_free(mod_func);
    }

    globus_hashtable_to_list(&attr->funcs.send_cb_table, &list);
    while(!globus_list_empty(list))
    {
        mod_func = (globus_i_gsc_module_func_t *)
            globus_list_remove(&list, list);
        globus_free(mod_func->key);
        globus_free(mod_func);
    }

    globus_hashtable_destroy(&attr->funcs.send_cb_table);
    globus_hashtable_destroy(&attr->funcs.recv_cb_table);

    globus_free(attr->base_dir);
    globus_free(attr->modes);
    globus_free(attr->types);
    globus_free(attr);

    GlobusGridFTPServerDebugExit();

    return GLOBUS_SUCCESS;

  err:

    GlobusGridFTPServerDebugExitWithError();

    return res;
}

globus_result_t
globus_gridftp_server_control_attr_copy(
    globus_gridftp_server_control_attr_t *  dst,
    globus_gridftp_server_control_attr_t    src)
{
    globus_result_t                         res;
    globus_i_gsc_attr_t *                   attr;
    GlobusGridFTPServerName(globus_gridftp_server_control_attr_copy);

    if(dst == NULL)
    {
        res = GlobusGridFTPServerErrorParameter("dst");
        goto err;
    }
    if(src == NULL)
    {
        res = GlobusGridFTPServerErrorParameter("src");
        goto err;
    }
    if(src->version_ctl != GLOBUS_GRIDFTP_VERSION_CTL)
    {
        res = GlobusGridFTPServerErrorParameter("in_attr");
        goto err;
    }

    attr = (globus_i_gsc_attr_t *) globus_malloc(
                sizeof(globus_i_gsc_attr_t));
    if(attr == NULL)
    {
        res = GlobusGridFTPServerControlErrorSystem();
        goto err;
    }
    attr->version_ctl = src->version_ctl;
    attr->funcs.resource_cb = src->funcs.resource_cb;
    globus_hashtable_copy(
	    &attr->funcs.send_cb_table, &src->funcs.send_cb_table, NULL);
    globus_hashtable_copy(
        &attr->funcs.recv_cb_table, &src->funcs.recv_cb_table, NULL);
    attr->modes = globus_libc_strdup(src->modes);
    attr->types = globus_libc_strdup(src->types);

    *dst = attr;

    return GLOBUS_SUCCESS;

  err:

    GlobusGridFTPServerDebugExitWithError();

    return res;
}

globus_result_t
globus_gridftp_server_control_attr_add_type(
    globus_gridftp_server_control_attr_t    in_attr,
    char                                    type)
{
    char                                    ch;
    char *                                  tmp_str;
    globus_i_gsc_attr_t *                   attr;
    globus_result_t                         res;
    GlobusGridFTPServerName(globus_gridftp_server_control_attr_add_type);

    GlobusGridFTPServerDebugEnter();
    if(in_attr == NULL)
    {
        res = GlobusGridFTPServerErrorParameter("in_attr");
        goto err;
    }
    attr = in_attr;

    ch = toupper(type);
    tmp_str = globus_common_create_string("%s%c", attr->types, ch);
    globus_free(attr->types);
    attr->types = tmp_str;

    GlobusGridFTPServerDebugExit();
    return GLOBUS_SUCCESS;

  err:

    GlobusGridFTPServerDebugExitWithError();

    return res;
}

globus_result_t
globus_gridftp_server_control_attr_set_security(
    globus_gridftp_server_control_attr_t    in_attr,
    globus_gridftp_server_control_security_type_t sec)
{
    globus_i_gsc_attr_t *                   attr;
    globus_result_t                         res;
    GlobusGridFTPServerName(globus_gridftp_server_control_attr_set_security);

    GlobusGridFTPServerDebugEnter();
    if(in_attr == NULL)
    {
        res = GlobusGridFTPServerErrorParameter("in_attr");
        goto err;
    }
    attr = in_attr;

    attr->security = sec;

    GlobusGridFTPServerDebugExit();
    return GLOBUS_SUCCESS;

  err:

    GlobusGridFTPServerDebugExitWithError();
    return res;
}

globus_result_t
globus_gridftp_server_control_attr_add_mode(
    globus_gridftp_server_control_attr_t    in_attr,
    char                                    mode)
{
    char                                    ch;
    char *                                  tmp_str;
    globus_i_gsc_attr_t *                   attr;
    globus_result_t                         res;
    GlobusGridFTPServerName(globus_gridftp_server_control_attr_add_mode);

    GlobusGridFTPServerDebugEnter();
    if(in_attr == NULL)
    {
        res = GlobusGridFTPServerErrorParameter("in_attr");
        goto err;
    }
    attr = in_attr;

    ch = toupper(mode);
    tmp_str = globus_common_create_string("%s%c", attr->modes, ch);
    globus_free(attr->modes);
    attr->modes = tmp_str;

    GlobusGridFTPServerDebugExit();
    return GLOBUS_SUCCESS;

  err:

    GlobusGridFTPServerDebugExitWithError();

    return res;
}

globus_result_t
globus_gridftp_server_control_attr_add_recv(
    globus_gridftp_server_control_attr_t    in_attr,
    const char *                            module_name,
    globus_gridftp_server_control_transfer_cb_t recv_cb,
    void *                                  user_arg)
{
    globus_i_gsc_module_func_t *            mod_func;
    globus_i_gsc_attr_t *                   attr;
    globus_result_t                         res;
    GlobusGridFTPServerName(globus_gridftp_server_control_attr_add_recv);

    GlobusGridFTPServerDebugEnter();

    if(in_attr == NULL)
    {
        res = GlobusGridFTPServerErrorParameter("in_attr");
        goto err;
    }
    attr = in_attr;

    if(recv_cb == NULL)
    {
        res = GlobusGridFTPServerErrorParameter("recv_cb");
        goto err;
    }

    if(attr->version_ctl != GLOBUS_GRIDFTP_VERSION_CTL)
    {
        res = GlobusGridFTPServerErrorParameter("in_attr");
        goto err;
    }

    if(module_name == NULL)
    {
        attr->funcs.default_recv_cb = recv_cb;
        attr->funcs.default_recv_arg = user_arg;
    }
    else
    {
        mod_func = (globus_i_gsc_module_func_t *) globus_malloc(
            sizeof(globus_i_gsc_module_func_t));
        if(mod_func == NULL)
        {
            res = GlobusGridFTPServerControlErrorSystem();
            goto err;
        }
        mod_func->func = recv_cb;
        mod_func->user_arg = user_arg;
        mod_func->key = globus_libc_strdup(module_name);
        globus_hashtable_insert(
            &attr->funcs.recv_cb_table,
            (void *)mod_func->key,
            mod_func);
    }
    GlobusGridFTPServerDebugExit();

    return GLOBUS_SUCCESS;

  err:

    GlobusGridFTPServerDebugExitWithError();

    return res;
}

globus_result_t
globus_gridftp_server_control_attr_add_send(
    globus_gridftp_server_control_attr_t    in_attr,
    const char *                            module_name,
    globus_gridftp_server_control_transfer_cb_t send_cb,
    void *                                  user_arg)
{
    globus_i_gsc_module_func_t *            mod_func;
    globus_i_gsc_attr_t *                   attr;
    globus_result_t                         res;
    GlobusGridFTPServerName(globus_gridftp_server_control_attr_add_send);

    GlobusGridFTPServerDebugEnter();

    if(in_attr == NULL)
    {
        res = GlobusGridFTPServerErrorParameter("in_attr");
        goto err;
    }
    attr = in_attr;

    if(send_cb == NULL)
    {
        res = GlobusGridFTPServerErrorParameter("send_cb");
        goto err;
    }

    if(attr->version_ctl != GLOBUS_GRIDFTP_VERSION_CTL)
    {
        res = GlobusGridFTPServerErrorParameter("in_attr");
        goto err;
    }

    if(module_name == NULL)
    {
        attr->funcs.default_send_cb = send_cb;
        attr->funcs.default_send_arg = user_arg;
    }
    else
    {
        mod_func = (globus_i_gsc_module_func_t *) globus_malloc(
            sizeof(globus_i_gsc_module_func_t));
        if(mod_func == NULL)
        {
            res = GlobusGridFTPServerControlErrorSystem();
            goto err;
        }
        mod_func->func = send_cb;
        mod_func->user_arg = user_arg;
        mod_func->key = globus_libc_strdup(module_name);
        globus_hashtable_insert(
            &attr->funcs.send_cb_table,
            (void *)mod_func->key,
            mod_func);
    }
    GlobusGridFTPServerDebugExit();

    return GLOBUS_SUCCESS;

  err:

    GlobusGridFTPServerDebugExitWithError();

    return res;
}

globus_result_t
globus_gridftp_server_control_attr_set_auth(
    globus_gridftp_server_control_attr_t    in_attr,
    globus_gridftp_server_control_auth_cb_t auth_cb,
    void *                                  user_arg)
{
    globus_i_gsc_attr_t *                   attr;
    globus_result_t                         res;
    GlobusGridFTPServerName(globus_gridftp_server_control_attr_set_auth);

    GlobusGridFTPServerDebugEnter();

    if(in_attr == NULL)
    {
        res = GlobusGridFTPServerErrorParameter("in_attr");
        goto err;
    }
    if(auth_cb == NULL)
    {
        res = GlobusGridFTPServerErrorParameter("auth_cb");
        goto err;
    }
    attr = in_attr;

    if(attr->version_ctl != GLOBUS_GRIDFTP_VERSION_CTL)
    {
        res = GlobusGridFTPServerErrorParameter("in_attr");
        goto err;
    }

    attr->funcs.auth_cb = auth_cb;
    attr->funcs.auth_arg = user_arg;

    GlobusGridFTPServerDebugExit();

    return GLOBUS_SUCCESS;

  err:

    GlobusGridFTPServerDebugExitWithError();

    return res;
}

globus_result_t
globus_gridftp_server_control_attr_set_resource(
    globus_gridftp_server_control_attr_t    in_attr,
    globus_gridftp_server_control_resource_cb_t resource_query_cb,
    void *                                  resource_arg)
{
    globus_i_gsc_attr_t *                   attr;
    globus_result_t                         res;
    GlobusGridFTPServerName(globus_gridftp_server_control_attr_resource_query);

    GlobusGridFTPServerDebugEnter();

    if(in_attr == NULL)
    {
        res = GlobusGridFTPServerErrorParameter("in_attr");
        goto err;
    }
    if(resource_query_cb == NULL)
    {
        res = GlobusGridFTPServerErrorParameter("resource_query_cb");
        goto err;
    }
    attr = in_attr;

    if(attr->version_ctl != GLOBUS_GRIDFTP_VERSION_CTL)
    {
        res = GlobusGridFTPServerErrorParameter("in_attr");
        goto err;
    }

    attr->funcs.resource_cb = resource_query_cb;
    attr->funcs.resource_arg = resource_arg;

    GlobusGridFTPServerDebugExit();

    return GLOBUS_SUCCESS;

  err:

    GlobusGridFTPServerDebugExitWithError();

    return res;
}

globus_result_t
globus_gridftp_server_control_attr_data_functions(
    globus_gridftp_server_control_attr_t                server_attr,
    globus_gridftp_server_control_active_connect_cb_t   active_cb,
    void *                                              active_arg,
    globus_gridftp_server_control_passive_connect_cb_t  passive_cb,
    void *                                              passive_arg,
    globus_gridftp_server_control_data_destroy_cb_t     destroy_cb,
    void *                                              destroy_arg)
{
    globus_i_gsc_attr_t *                   attr;
    globus_result_t                         res;
    GlobusGridFTPServerName(globus_gridftp_server_control_attr_set_passive);

    if(server_attr == NULL)
    {
        res = GlobusGridFTPServerErrorParameter("server_attr");
        goto err;
    }
    attr = (globus_i_gsc_attr_t *) server_attr;

    attr->funcs.passive_cb = passive_cb;
    attr->funcs.passive_arg = passive_arg;
    attr->funcs.active_cb = active_cb;
    attr->funcs.active_arg = active_arg;
    attr->funcs.data_destroy_cb = destroy_cb;
    attr->funcs.data_destroy_arg = destroy_arg;

    return GLOBUS_SUCCESS;

  err:

    return res;
}

globus_result_t
globus_gridftp_server_control_attr_set_log(
    globus_gridftp_server_control_attr_t    server_attr,
    globus_gridftp_server_control_log_cb_t  log_func,
    int                                     log_mask,
    void *                                  user_arg)
{
    globus_i_gsc_attr_t *                   attr;
    globus_result_t                         res;
    GlobusGridFTPServerName(globus_gridftp_server_control_attr_set_log);

    if(server_attr == NULL)
    {
        res = GlobusGridFTPServerErrorParameter("server_attr");
        goto err;
    }
    attr = (globus_i_gsc_attr_t *) server_attr;

    attr->funcs.log_func = log_func;
    attr->funcs.log_mask = log_mask;
    attr->funcs.log_arg = user_arg;

    return GLOBUS_SUCCESS;

  err:

    return res;
}

globus_result_t
globus_gridftp_server_control_attr_set_list(
    globus_gridftp_server_control_attr_t    in_attr,
    globus_gridftp_server_control_list_cb_t list_cb,
    void *                                  user_arg)
{
    globus_i_gsc_attr_t *                   attr;
    GlobusGridFTPServerName(globus_gridftp_server_control_attr_set_list);

    if(in_attr == NULL)
    {
        return GlobusGridFTPServerErrorParameter("server_attr");
    }
    attr = (globus_i_gsc_attr_t *) in_attr;

    attr->funcs.list_cb = list_cb;
    attr->funcs.list_arg = user_arg;

    return GLOBUS_SUCCESS;
}

globus_result_t
globus_gridftp_server_control_attr_set_banner(
    globus_gridftp_server_control_attr_t    in_attr,
    char *                                  banner)
{
    globus_i_gsc_attr_t *                   attr;
    GlobusGridFTPServerName(globus_gridftp_server_control_attr_set_list);

    if(in_attr == NULL)
    {
        return GlobusGridFTPServerErrorParameter("server_attr");
    }
    attr = (globus_i_gsc_attr_t *) in_attr;

    attr->pre_auth_banner = strdup(banner);

    return GLOBUS_SUCCESS;
}
                                                                                
globus_result_t
globus_gridftp_server_control_attr_set_idle_time(
    globus_gridftp_server_control_attr_t    in_attr,
    int                                     idle_timeout,
    int                                     preauth_timeout)
{
    globus_i_gsc_attr_t *                   attr;
    GlobusGridFTPServerName(globus_gridftp_server_control_attr_set_list);

    if(in_attr == NULL)
    {
        return GlobusGridFTPServerErrorParameter("server_attr");
    }
    attr = (globus_i_gsc_attr_t *) in_attr;

    attr->idle_timeout = idle_timeout;
    attr->preauth_timeout = preauth_timeout;

    return GLOBUS_SUCCESS;
}

globus_result_t
globus_gridftp_server_control_attr_set_epsv_ip(
    globus_gridftp_server_control_attr_t    in_attr,
    globus_bool_t                           epsv_ip)
{
    globus_i_gsc_attr_t *                   attr;
    GlobusGridFTPServerName(globus_gridftp_server_control_attr_set_epsv_ip);

    if(in_attr == NULL)
    {
        return GlobusGridFTPServerErrorParameter("server_attr");
    }
    attr = (globus_i_gsc_attr_t *) in_attr;

    attr->epsv_ip = epsv_ip;

    return GLOBUS_SUCCESS;
}

globus_result_t
globus_gridftp_server_control_attr_set_message(
    globus_gridftp_server_control_attr_t    in_attr,
    char *                                  message)
{
    globus_i_gsc_attr_t *                   attr;
    GlobusGridFTPServerName(globus_gridftp_server_control_attr_set_list);

    if(in_attr == NULL)
    {
        return GlobusGridFTPServerErrorParameter("server_attr");
    }
    if(message == NULL)
    {
        return GlobusGridFTPServerErrorParameter("message");
    }
    attr = (globus_i_gsc_attr_t *) in_attr;

    attr->post_auth_banner = strdup(message);

    return GLOBUS_SUCCESS;
}
