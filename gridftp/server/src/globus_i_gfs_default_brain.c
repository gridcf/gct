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

#include "globus_gfork.h"
#include "globus_gridftp_server.h"
#include "globus_i_gfs_ipc.h"
#include "gfs_i_gfork_plugin.h"

#define GFS_BRAIN_FIXED_SIZE    256
#define GFS_DB_REPO_SIZE        16
#define GFS_DB_REPO_NAME        "default"
#define STATIC_TIMEOUT          10

typedef enum gfs_l_db_node_type_e
{
    GFS_DB_NODE_TYPE_STATIC = 1,
    GFS_DB_NODE_TYPE_DYNAMIC
} gfs_l_db_node_type_t;

typedef struct gfs_l_db_node_s
{
    /* over load globus_i_gfs_brain_node_t */
    char *                              host_id;
    char *                              repo_name;
    void *                              brain_arg;
    int                                 max_connection;
    int                                 current_connection;
    int                                 total_max_connections;
    int                                 total_connections;
    float                               load;
    /* end over load */
    gfs_l_db_node_type_t                type;
    globus_bool_t                       error;
    char *                              cookie_id;
    struct gfs_l_db_repo_s *            repo;
} gfs_l_db_node_t;

typedef struct gfs_l_db_repo_s
{
    char *                              name;
    globus_hashtable_t                  node_table;
    globus_priority_q_t                 node_q;
} gfs_l_db_repo_t;

static globus_mutex_t                   globus_l_brain_mutex;
static globus_hashtable_t               gfs_l_db_repo_table;
static gfs_l_db_repo_t *                gfs_l_db_default_repo = NULL;

static gfork_child_handle_t             globus_l_gfs_gfork_handle;
static globus_bool_t                    globus_l_gfs_gfork_ready = GLOBUS_FALSE;
static globus_callback_func_t           globus_l_gfs_gfork_ready_cb = NULL;
static void *                           globus_l_gfs_gfork_ready_cb_arg;
static globus_bool_t                    globus_l_gfs_gfork_on = GLOBUS_FALSE;

static
int
gfs_l_db_node_cmp(
    void *                              priority_1,
    void *                              priority_2)
{
    gfs_l_db_node_t *                   n1;
    gfs_l_db_node_t *                   n2;

    n1 = (gfs_l_db_node_t *) priority_1;
    n2 = (gfs_l_db_node_t *) priority_2;

    /* if the node is saturated always put it at the back of the queue */
    if(n1->current_connection >= n1->max_connection && n1->max_connection != 0)
    {
        return 1;
    }
    /* if the total number of connections is capped and we have exceded
        the cap send it to the back */
    if(n1->total_max_connections > 0
        && n1->total_connections >= n1->total_max_connections)
    {
        return 1;
    }
    if(n2->current_connection >= n2->max_connection && n2->max_connection != 0)
    {
        return -1;
    }
    if(n1->current_connection < n2->current_connection)
    {
        return -1;
    }
    else if(n1->current_connection == n2->current_connection)
    {
        return 0;
    }
    else
    {
        return 1;
    }
}

static
globus_list_t *
gfs_l_db_parse_string_list(
    const char *                        str_list)
{
    char *                              last_str;
    char *                              tmp_str;
    globus_list_t *                     list = NULL;
    char *                              p;

    if(str_list == NULL || *str_list == '\0')
    {
        return NULL;
    }

    tmp_str = globus_libc_strdup(str_list);
    last_str = tmp_str;
    while((p = strchr(last_str, ',')) != NULL)
    {
        *p = '\0';
        globus_list_insert(&list, strdup(last_str));
        last_str = p + 1;
    }
    globus_list_insert(&list, strdup(last_str));

    globus_free(tmp_str);

    return list;
}

/*
static
void
globus_l_brain_log_socket(
    globus_xio_handle_t                 handle,
    char *                              msg)
{
    globus_result_t                     res;
    char *                              peer_contact;

    res = globus_xio_handle_cntl(
        handle,
        globus_i_gfs_tcp_driver,
        GLOBUS_XIO_TCP_GET_REMOTE_NUMERIC_CONTACT,
        &peer_contact);
    if(res != GLOBUS_SUCCESS)
    {
        peer_contact = strdup("could get peer addr");
    }

    globus_gfs_log_message(
        GLOBUS_GFS_LOG_WARN,
        "[%s]  %s", peer_contact, msg);
    globus_free(peer_contact);
}
*/

static
void
globus_l_gfs_gfork_error_cb(
    gfork_child_handle_t                handle,
    void *                              user_arg,
    globus_result_t                     result)
{
    globus_gfs_log_message(GLOBUS_GFS_LOG_WARN, "GFork closed.");
}



static
void
globus_l_gfs_gfork_close_cb(
    gfork_child_handle_t                handle,
    void *                              user_arg,
    pid_t                               from_pid)
{
    globus_gfs_log_message(GLOBUS_GFS_LOG_WARN, "GFork closed.");
}

static
void
globus_l_gfs_gfork_dyn_reg(
    gfork_child_handle_t                handle,
    void *                              user_arg,
    pid_t                               from_pid,
    globus_byte_t *                     buffer,
    globus_size_t                       len)
{
    uint32_t                            tmp_32;
    gfs_l_db_node_t *                   node = NULL;
    gfs_l_db_repo_t *                   repo = NULL;
    int                                 con_max;
    int                                 total_max;
    char                                repo_name[GF_DYN_REPO_LEN];
    char                                cs[GF_DYN_CS_LEN];
    char                                cookie[GF_DYN_COOKIE_LEN];
    char *                              cookie_id;
    GlobusGFSName(globus_l_gfs_gfork_dyn_reg);

    globus_gfs_log_message(
        GLOBUS_GFS_LOG_WARN,
        "[%s] enter", "globus_l_gfs_gfork_dyn_reg");

    memcpy(&tmp_32, &buffer[GF_DYN_AT_ONCE_NDX], sizeof(uint32_t));
    con_max = (int) tmp_32;

    memcpy(&tmp_32, &buffer[GF_DYN_TOTAL_NDX], sizeof(uint32_t));
    total_max = (int) tmp_32;
    if(total_max == 0)
    {
        total_max = -1;
    }

    memcpy(cookie, &buffer[GF_DYN_COOKIE_NDX], GF_DYN_COOKIE_LEN);
    memcpy(cs, &buffer[GF_DYN_CS_NDX], GF_DYN_CS_LEN);
    memcpy(repo_name, &buffer[GF_DYN_REPO_NDX], GF_DYN_REPO_LEN);

    if(*cs == '\0')
    {
        goto error_cs;
    }
    if(repo_name[0] == '\0')
    {
        strcpy(repo_name, GFS_DB_REPO_NAME);
    }
    cookie_id = globus_common_create_string("%s::%s", cookie, cs);

    repo = (gfs_l_db_repo_t *) globus_hashtable_lookup(
        &gfs_l_db_repo_table, repo_name);
    if(repo == NULL)
    {
        /* create a new repo */
        repo = (gfs_l_db_repo_t *) calloc(1, sizeof(gfs_l_db_repo_t));
        globus_priority_q_init(&repo->node_q, gfs_l_db_node_cmp);
        globus_hashtable_init(
            &repo->node_table,
            32,
            globus_hashtable_string_hash,
            globus_hashtable_string_keyeq);

        repo->name = strdup(repo_name);
        globus_hashtable_insert(&gfs_l_db_repo_table, repo->name, repo);
    }
    else
    {
        node = (gfs_l_db_node_t *)
            globus_hashtable_lookup(&repo->node_table, cookie_id);
    }

    if(node == NULL)
    {
        node = (gfs_l_db_node_t*)globus_calloc(1, sizeof(gfs_l_db_node_t));
        node->host_id = strdup(cs);
        node->cookie_id = cookie_id;
        node->repo_name = strdup(repo_name);
        node->repo = repo;
        globus_priority_q_enqueue(&repo->node_q, node, node);
        globus_hashtable_insert(&repo->node_table, node->cookie_id, node);
        /* the next line is here so that if it was static it will
            remain static */
        node->type = GFS_DB_NODE_TYPE_DYNAMIC;
        node->current_connection = 0;
        node->max_connection = con_max;
        node->total_max_connections = total_max;
        globus_gfs_log_message(
            GLOBUS_GFS_LOG_WARN,
            "A new backend registered, contact string: [%s] %s\n"
            "  max=[%d]\n  total=[%d]\n id=[%s]\n",
            node->repo_name,
            node->host_id,
            node->max_connection,
            node->total_max_connections,
            node->cookie_id);
    }
    else
    {
        /* XXX ? do i need to dequeue and requeue ? */
        node->total_max_connections = total_max;
        node->max_connection = con_max;
        free(cookie_id);
    }
error_cs:
    return;
}

static
void
gfs_l_brain_killer_cb(
    void *                              user_arg)
{
    exit(2);
}

static
void
globus_l_gfs_mem_release_write_cb(
    globus_xio_handle_t                 handle,
    globus_result_t                     result,
    globus_xio_iovec_t *                iovec,
    int                                 count,
    globus_size_t                       nbytes,
    globus_xio_data_descriptor_t        data_desc,
    void *                              user_arg)
{
    globus_byte_t *                     buffer;

    buffer = (globus_byte_t *) user_arg;

    globus_free(buffer);
}

static
void
globus_l_gfs_mem_limit_release_cb(
    const char *                        opt_name,
    int                                 val,
    void *                              user_arg)
{
    globus_xio_iovec_t                  iov[1];
    globus_result_t                     result;
    globus_byte_t *                     buffer;
    uint32_t                            tmp32;

    if(globus_l_gfs_gfork_on)
    {
        globus_mutex_lock(&globus_l_brain_mutex);
        {
            tmp32 = (uint32_t)val;
            buffer = globus_malloc(GF_RELEASE_MSG_LEN);
            buffer[GF_VERSION_NDX] = GF_VERSION;
            buffer[GF_MSG_TYPE_LEN] = GFS_GFORK_MSG_TYPE_RELEASE;
            memcpy(&buffer[GF_RELEASE_COUNT_NDX], &tmp32, sizeof(uint32_t));

            iov[0].iov_base = buffer;
            iov[0].iov_len = GF_RELEASE_MSG_LEN;

            result = globus_gfork_send(
                globus_l_gfs_gfork_handle,
                -1, /* to the master */
                iov,
                1,
                globus_l_gfs_mem_release_write_cb,
                buffer);
        }
        globus_mutex_unlock(&globus_l_brain_mutex);
    }
}


static
void
globus_l_gfs_gfork_incoming_cb(
    gfork_child_handle_t                handle,
    void *                              user_arg,
    pid_t                               from_pid,
    globus_byte_t *                     buffer,
    globus_size_t                       len)
{
    globus_size_t                       off;
    globus_i_gfs_config_option_cb_ent_t * cb_handle;
    globus_reltime_t                    delay;
    uint32_t                            n32;
    int                                 count;
    int                                 i;
    char *                              tmp_buf;
    GlobusGFSName(globus_l_gfs_gfork_incoming_cb);


    globus_gfs_log_message(
        GLOBUS_GFS_LOG_WARN,
        "[%s] enter", "globus_l_gfs_gfork_incoming_cb()");
    globus_mutex_lock(&globus_l_brain_mutex);
    {
        /* verify message */
        if(buffer[GF_VERSION_NDX] != GF_VERSION)
        {
            goto error;
        }

        switch(buffer[GF_MSG_TYPE_NDX])
        {
            case GFS_GFORK_MSG_TYPE_DYNBE:

                off = 0;
                memcpy(&n32, &buffer[GF_DYN_ENTRY_COUNT_NDX], sizeof(uint32_t));
                count = n32;
                globus_gfs_log_message(
                        GLOBUS_GFS_LOG_ERR,
                        "[%s] received %d registrations\n",
                            "globus_l_gfs_gfork_incoming_cb()", n32);
                for(i = 0; i < count && len >= GF_DYN_PACKET_LEN; i++)
                {
                    tmp_buf = (char *) &buffer[off];
                    memcpy(&n32,
                        &tmp_buf[GF_DYN_ENTRY_COUNT_NDX], sizeof(uint32_t));

                    if(len < GF_DYN_PACKET_LEN)
                    {
                        globus_gfs_log_message(
                            GLOBUS_GFS_LOG_ERR,
                            "[%s] error in dyn reg size\n",
                            "globus_l_gfs_gfork_incoming_cb()");
                    }
                    else
                    {
                        globus_gfs_log_message(
                            GLOBUS_GFS_LOG_INFO,
                            "[%s] registering a new backend %d\n",
                            "globus_l_gfs_gfork_incoming_cb()",
                            n32);
                        globus_l_gfs_gfork_dyn_reg(
                            handle,
                            user_arg,
                            from_pid,
                            (globus_byte_t *) tmp_buf,
                            GF_DYN_PACKET_LEN);
                    }
                    off += GF_DYN_PACKET_LEN;
                    len -= GF_DYN_PACKET_LEN;
                }
                globus_free(buffer);

                break;

            case GFS_GFORK_MSG_TYPE_MEM:
                memcpy(&n32, &buffer[GF_MEM_LIMIT_NDX], sizeof(uint32_t));
                globus_gfs_config_set_int("tcp_mem_limit", (int)n32);

                globus_gfs_log_message(
                    GLOBUS_GFS_LOG_WARN,
                    "TCP mem limit set to: %d\n", (int)n32);

                globus_gfs_config_add_cb(
                    &cb_handle,
                    "tcp_mem_limit",
                    globus_l_gfs_mem_limit_release_cb,
                    NULL);
                break;

            case GFS_GFORK_MSG_TYPE_READY:
                globus_gfs_log_message(
                    GLOBUS_GFS_LOG_WARN, "Ready message received.\n");
                globus_l_gfs_gfork_ready = GLOBUS_TRUE;
                if(globus_l_gfs_gfork_ready_cb != NULL)
                {
                    globus_callback_register_oneshot(
                        NULL,
                        NULL,
                        globus_l_gfs_gfork_ready_cb,
                        globus_l_gfs_gfork_ready_cb_arg);
                }
                break;

            case GFS_GFORK_MSG_TYPE_KILL:
                n32 = 1024;
                globus_i_gfs_control_end_421(
                    "421 Server load too high. Try again later.\r\n");

                globus_gfs_config_set_int("tcp_mem_limit", (int)n32);
                GlobusTimeReltimeSet(delay, 2, 0);
                globus_callback_register_oneshot(
                    NULL,
                    &delay,
                    gfs_l_brain_killer_cb,
                    NULL);
                break;

            default:
               globus_gfs_log_message(
                    GLOBUS_GFS_LOG_WARN,
                    "Unknown GFork message type received.\n");
                break;
        }
    }
    globus_mutex_unlock(&globus_l_brain_mutex);

    return;

error:
    globus_free(buffer);
    globus_mutex_unlock(&globus_l_brain_mutex);
}

static
globus_result_t
globus_l_gfs_default_brain_init(
    globus_callback_func_t              ready_cb,
    void *                              ready_cb_arg)
{
    gfs_l_db_repo_t *                   default_repo;
    char *                              remote_list;
    globus_list_t *                     list;
    gfs_l_db_node_t *                   node;
    globus_result_t                     result;
    int                                 rc;

    rc = globus_module_activate(GLOBUS_GFORK_CHILD_MODULE);
    if(rc != 0)
    {
        globus_gfs_log_message(
            GLOBUS_GFS_LOG_WARN,
            "Could not activate GFork\n");
    }

    globus_mutex_init(&globus_l_brain_mutex, NULL);

    globus_mutex_lock(&globus_l_brain_mutex);
    {
        globus_hashtable_init(
            &gfs_l_db_repo_table,
            GFS_DB_REPO_SIZE,
            globus_hashtable_string_hash,
            globus_hashtable_string_keyeq);

        remote_list = globus_i_gfs_config_string("remote_nodes");
        list = gfs_l_db_parse_string_list(remote_list);

        default_repo = (gfs_l_db_repo_t *) globus_calloc(
            1, sizeof(gfs_l_db_repo_t));
        default_repo->name = strdup(GFS_DB_REPO_NAME);
        gfs_l_db_default_repo = default_repo;
        globus_priority_q_init(&default_repo->node_q, gfs_l_db_node_cmp);

        globus_hashtable_init(
            &gfs_l_db_default_repo->node_table,
            32,
            globus_hashtable_string_hash,
            globus_hashtable_string_keyeq);
        while(!globus_list_empty(list))
        {
            node =(gfs_l_db_node_t *)globus_calloc(1, sizeof(gfs_l_db_node_t));

            node->host_id = (char *) globus_list_remove(&list, list);
            node->repo_name = strdup(default_repo->name);
            node->max_connection = 0;
            node->total_max_connections = -1; /* -1 is infinite */
            node->current_connection = 0;
            node->load = 0.0;
            node->error = GLOBUS_FALSE;
            node->type = GFS_DB_NODE_TYPE_STATIC;
            node->repo = default_repo;
            node->cookie_id = globus_common_create_string("STATIC::%s",
                node->host_id);

            globus_hashtable_insert(
                &gfs_l_db_default_repo->node_table, node->cookie_id, node);
            globus_priority_q_enqueue(&default_repo->node_q, node, node);
        }
        globus_hashtable_insert(
            &gfs_l_db_repo_table, default_repo->name, default_repo);

        globus_gfs_config_set_int("data_connection_max", -1);


        globus_l_gfs_gfork_ready_cb = ready_cb;
        globus_l_gfs_gfork_ready_cb_arg = ready_cb_arg;
        /* try setting up gfork stuff */
        result = globus_gfork_child_worker_start(
            &globus_l_gfs_gfork_handle,
            NULL,
            globus_l_gfs_gfork_close_cb,
            globus_l_gfs_gfork_incoming_cb,
            globus_l_gfs_gfork_error_cb,
            NULL);
        if(result != GLOBUS_SUCCESS)
        {
            globus_gfs_log_result(
                GLOBUS_GFS_LOG_DUMP,
                "GFork functionality not enabled.",
                result);

            globus_l_gfs_gfork_ready = GLOBUS_TRUE;

            if(globus_l_gfs_gfork_ready_cb != NULL)
            {
                globus_callback_register_oneshot(
                    NULL,
                    NULL,
                    globus_l_gfs_gfork_ready_cb,
                    globus_l_gfs_gfork_ready_cb_arg);
            }
        }
        else
        {
            globus_l_gfs_gfork_on = GLOBUS_TRUE;
        }
    }
    globus_mutex_unlock(&globus_l_brain_mutex);

    return GLOBUS_SUCCESS;
}

static
void
globus_l_gfs_default_brain_stop()
{
    globus_gfork_child_stop(globus_l_gfs_gfork_handle);
    globus_mutex_destroy(&globus_l_brain_mutex);
}

static
globus_result_t
globus_l_gfs_default_brain_available(
    const char *                        user_id,
    const char *                        repo_name,
    int *                               count)
{
    int                                 size;

    size = globus_priority_q_size(&gfs_l_db_default_repo->node_q);
    *count = size;

    return GLOBUS_SUCCESS;
}

static
globus_result_t
globus_l_gfs_default_brain_select_nodes(
    globus_i_gfs_brain_node_t ***       out_node_array,
    int *                               out_array_length,
    const char *                        r_name,
    globus_off_t                        filesize,
    int                                 min_count,
    int                                 max_count)
{
    globus_bool_t                       done = GLOBUS_FALSE;
    int                                 best_count;
    int                                 count;
    int                                 e_count;
    int                                 loop_count;
    int                                 i;
    globus_i_gfs_brain_node_t **        node_array;
    gfs_l_db_node_t *                   node;

    globus_result_t                     result;
    gfs_l_db_repo_t *                   repo = NULL;
    char *                              repo_name;
    GlobusGFSName(globus_gfs_brain_select_nodes);

    if(min_count < 1)
    {
        result = globus_error_put(GlobusGFSErrorObjParameter("min_count"));
        goto error_parameter;
    }

    globus_gfs_log_message(
        GLOBUS_GFS_LOG_INFO,
        "[%s] enter", "globus_l_gfs_default_brain_select_nodes\n");

    repo_name = (char *) r_name;
    if(repo_name == NULL || *repo_name =='\0')
    {
        repo_name = GFS_DB_REPO_NAME;
    }
    globus_mutex_lock(&globus_l_brain_mutex);
    {
        repo = (gfs_l_db_repo_t *) globus_hashtable_lookup(
            &gfs_l_db_repo_table, repo_name);
        if(repo == NULL)
        {
            result = globus_error_put(GlobusGFSErrorObjParameter("repo_name"));
            goto error;
        }

        best_count = globus_i_gfs_config_int("stripe_count");
        if(best_count > max_count || best_count <= 0)
        {
            best_count = max_count;
        }
        if(best_count < min_count)
        {
            best_count = min_count;
        }

        node_array = (globus_i_gfs_brain_node_t **)
            globus_calloc(max_count, sizeof(globus_i_gfs_brain_node_t *));
        if(node_array == NULL)
        {
            result = globus_error_put(GlobusGFSErrorObjMemory("nodes"));
            goto error;
        }
        count = 0;
        e_count = count;

        do
        {
            loop_count = 0;
            done = GLOBUS_FALSE;
            while(!done && count < best_count)
            {
                node = (gfs_l_db_node_t *)
                    globus_priority_q_dequeue(&repo->node_q);
                if(node == NULL)
                {
                    done = GLOBUS_TRUE;
                }
                /* if we have exceed the current connection count for a node,
                    or we have exceeded the total connection count for the
                    node, we do not use it */
                else if(
                    (node->current_connection >= node->max_connection &&
                     node->max_connection < 0) ||
                    (node->total_max_connections > 0 &&
                        node->total_connections >= node->total_max_connections))
                {
                    /* need to up everything for sake of nice clean up*/
                    node->current_connection++;
                    node_array[count] = (globus_i_gfs_brain_node_t *) node;
                    e_count = count + 1;
                    done = GLOBUS_TRUE;
                }
                else
                {
                    node->current_connection++;
                    node_array[count] = (globus_i_gfs_brain_node_t *) node;
                    loop_count++;
                    count++;
                    e_count = count;
                }
            }
            if(done && count == 0)
            {
                result = globus_error_put(
                    GlobusGFSErrorObjParameter("No nodes available."));
                goto error_short;
            }

           /* if we are here we were successful and must re-enqueue the nodes
            * we just took out with new order */
            for(i = count - loop_count; i < count; i++)
            {
                node = (gfs_l_db_node_t *) node_array[i];
                node->total_connections++;
                globus_priority_q_enqueue(
                    &repo->node_q, node_array[i], node_array[i]);
            }
        }
        while(!(done && count == 0) && count < min_count);

        *out_node_array = node_array;
        *out_array_length = count;
    }
    globus_mutex_unlock(&globus_l_brain_mutex);

    return GLOBUS_SUCCESS;

error_short:
    /* remove the connectiopn reference */
    for(i = 0; i < e_count; i++)
    {
        node = (gfs_l_db_node_t *) node_array[i];
        node->current_connection--;
        globus_priority_q_enqueue(&repo->node_q, node, node);
        globus_gfs_log_message(
            GLOBUS_GFS_LOG_WARN,
            "Not enough nodes available: [%s] %s: %d, %d, %d\n",
            node->repo_name,
            node->host_id,
            node->current_connection,
            node->max_connection,
            node->total_max_connections);
    }
    globus_free(node_array);
error:
    globus_mutex_unlock(&globus_l_brain_mutex);
error_parameter:
    {
        char * msg = globus_error_print_friendly(globus_error_peek(result));
        globus_gfs_log_message(
            GLOBUS_GFS_LOG_WARN,
            "No nodes given because parameter error: %d: %d: %s\n",
            min_count, count, msg);
        free(msg);
    }
    return result;
}

static
void
gfs_l_db_static_error_timeout(
    void *                              arg)
{
    gfs_l_db_repo_t *                   repo;
    gfs_l_db_node_t *                   node;

    node = (gfs_l_db_node_t *) arg;
    repo = node->repo;
    globus_mutex_lock(&globus_l_brain_mutex);
    {
        /* clear the error and let it try again */
        node->error = GLOBUS_FALSE;
        globus_priority_q_enqueue(&repo->node_q, node, node);
        globus_hashtable_insert(&repo->node_table, node->cookie_id, node);
    }
    globus_mutex_unlock(&globus_l_brain_mutex);
}

static
globus_result_t
globus_l_gfs_default_brain_release_node(
    globus_i_gfs_brain_node_t *         b_node,
    globus_gfs_brain_reason_t           reason)
{
    int                                 dn_count = 0;
    globus_list_t *                     list;
    void *                              tmp_ptr;
    gfs_l_db_repo_t *                   repo;
    globus_bool_t                       first_error = GLOBUS_FALSE;
    globus_bool_t                       done;
    globus_result_t                     result;
    gfs_l_db_node_t *                   node;
    gfs_l_db_node_t *                   tmp_node;
    void *                              tmp_nptr;
    GlobusGFSName(globus_l_gfs_default_brain_release_node);

    node = (gfs_l_db_node_t *) b_node;

    globus_mutex_lock(&globus_l_brain_mutex);
    {
        repo = node->repo;
        node->current_connection--;
        tmp_ptr  = globus_priority_q_remove(&repo->node_q, node);
        if(tmp_ptr == NULL && !node->error)
        {
            result = GlobusGFSErrorGeneric("not a valid node");
            goto error;
        }
        switch(reason)
        {
            case GLOBUS_GFS_BRAIN_REASON_ERROR:
                globus_gfs_log_message(
                    GLOBUS_GFS_LOG_WARN,
                    "Node released with error: [%s] %s\n",
                    node->repo_name,
                    node->host_id);
                first_error = !node->error;
                node->error = GLOBUS_TRUE;
                break;

            case GLOBUS_GFS_BRAIN_REASON_COMPLETE:
                first_error = GLOBUS_FALSE;
                break;

            default:
                globus_assert(0);
                break;
        }
        if(node->error)
        {
            tmp_nptr = globus_hashtable_remove(
                &repo->node_table, node->cookie_id);
            assert(tmp_nptr == node || tmp_nptr == NULL);
            if(node->type == GFS_DB_NODE_TYPE_DYNAMIC)
            {
                if(node->current_connection == 0)
                {
                    globus_free(node->repo_name);
                    globus_free(node->host_id);
                    globus_free(node);
                }
            }
            else if(first_error)
            {
                globus_reltime_t        delay;
                GlobusTimeReltimeSet(delay, STATIC_TIMEOUT, 0);
                globus_callback_register_oneshot(
                    NULL,
                    &delay,
                    gfs_l_db_static_error_timeout,
                    node);
            }
            /* re-count everything and set it again in config,
                we need to recount because a single unlimited sets the
                value to unlimited.  if we are removing an unlimited it
                may no longer be unlimited, but it may still be */
            globus_hashtable_to_list(&repo->node_table, &list);
            done = GLOBUS_FALSE;
            while(!globus_list_empty(list) && !done)
            {
                tmp_node = (gfs_l_db_node_t *) globus_list_first(list);
                dn_count += tmp_node->max_connection;
                if(tmp_node->max_connection < 0)
                {
                    done = GLOBUS_TRUE;
                    dn_count = -1;
                }
                list = globus_list_rest(list);
            }
            globus_gfs_config_set_int("data_connection_max", dn_count);
        }
        else
        {
            /* if the node is not all used up */
            if(node->total_max_connections < 0
                || node->total_connections < node->total_max_connections)
            {
                globus_priority_q_enqueue(&repo->node_q, node, node);
            }
            else
            {
                tmp_nptr = globus_hashtable_remove(
                    &repo->node_table, node->cookie_id);
                assert(tmp_nptr == node || tmp_nptr == NULL);
                globus_assert(node->current_connection == 0);
                globus_free(node->cookie_id);
                globus_free(node->repo_name);
                globus_free(node->host_id);
                globus_free(node);
            }
        }
    }
    globus_mutex_unlock(&globus_l_brain_mutex);
    /* depending on reason we may remove from list or whatever */

    return GLOBUS_SUCCESS;
error:
    return result;
}

globus_i_gfs_brain_module_t globus_i_gfs_default_brain =
{
    globus_l_gfs_default_brain_init,
    globus_l_gfs_default_brain_stop,
    globus_l_gfs_default_brain_select_nodes,
    globus_l_gfs_default_brain_release_node,
    globus_l_gfs_default_brain_available
};
