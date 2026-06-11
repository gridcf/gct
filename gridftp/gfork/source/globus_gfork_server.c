#include "globus_common.h"
#include "globus_i_gfork.h"
#include "errno.h"
#include <sys/types.h>
#include <sys/wait.h>

#define GFORK_WAIT_FOR_KILL         2

extern char **environ;

char *                                  gfork_l_keep_envs[] =
{
    "X509_CERT_DIR",
    "X509_USER_PROXY",
    "GRIDMAP",
    "GLOBUS_HOSTNAME",
    "GLOBUS_TCP_PORT_RANGE",
    "X509_USER_CERT",
    "X509_USER_KEY",
    NULL
};

static globus_mutex_t                   gfork_l_mutex;
static globus_cond_t                    gfork_l_cond;
static globus_hashtable_t               gfork_l_pid_table;
static globus_hashtable_t               gfork_l_master_pid_table;
static globus_bool_t                    gfork_l_done = GLOBUS_FALSE;

static globus_list_t *                  gfork_l_pid_list = NULL;

static gfork_i_options_t                gfork_l_options;
static gfork_i_handle_t                 gfork_l_handle;
static char *                           g_contact_string;

static globus_hashtable_t               gfork_l_keepenvs;

static int                              gfork_l_connection_count = 0;
static globus_bool_t                    gfork_l_accepting;

static
void
gfork_l_server_accept_cb(
    globus_xio_server_t                 server,
    globus_xio_handle_t                 handle,
    globus_result_t                     result,
    void *                              user_arg);

static
globus_bool_t
gfork_accept_allowed()
{
    if(gfork_l_options.instances <= 0)
    {
        return GLOBUS_TRUE;
    }

    return (gfork_l_connection_count < gfork_l_options.instances);
}

static
void
gfork_gather_envs()
{
    int                                 i;
    char *                              env_s;

    globus_hashtable_init(
        &gfork_l_keepenvs,
        128,
        globus_hashtable_string_hash,
        globus_hashtable_string_keyeq);

    for(i = 0; gfork_l_keep_envs[i] != NULL; i++)
    {
        env_s = getenv(gfork_l_keep_envs[i]);
        if(env_s != NULL)
        {
            globus_hashtable_insert(
                &gfork_l_keepenvs,
                gfork_l_keep_envs[i],
                env_s);
        }
    }
}



static
globus_list_t *
gfork_kid_set_keeper_envs(
    globus_list_t *                     in_list,
    int                                 read_fd,
    int                                 write_fd)
{
    int                                 i;
    char *                              val_s;
    char *                              kv_pair;
    globus_list_t *                     new_list = NULL;
    globus_list_t *                     list;

    for(list = in_list; !globus_list_empty(list); list = globus_list_rest(list))
    {
        char * val_s = (char *) globus_list_first(list);

        globus_list_insert(&new_list, globus_libc_strdup(val_s));
    }


    for(i = 0; gfork_l_keep_envs[i] != NULL; i++)
    {
        val_s = globus_hashtable_lookup(
            &gfork_l_keepenvs,
            gfork_l_keep_envs[i]);
        if(val_s != NULL)
        {
            kv_pair = globus_common_create_string("%s=%s",
                gfork_l_keep_envs[i], val_s);
            globus_list_insert(&new_list, kv_pair);
        }
    }

    /* set extra envs */
    kv_pair = globus_common_create_string("%s=%d",
        GFORK_CHILD_READ_ENV, read_fd);
    globus_list_insert(&new_list, kv_pair);
    kv_pair = globus_common_create_string("%s=%d",
        GFORK_CHILD_WRITE_ENV, write_fd);
    globus_list_insert(&new_list, kv_pair);
    kv_pair = globus_common_create_string("%s=%s",
        GFORK_CHILD_CS_ENV, g_contact_string);
    globus_list_insert(&new_list, kv_pair);
    kv_pair = globus_common_create_string("%s=%d",
        GFORK_CHILD_INSTANCE_ENV, gfork_l_options.instances);
    globus_list_insert(&new_list, kv_pair);

    return new_list;
}

static
void
gfork_l_writev_cb(
    globus_xio_handle_t                 handle,
    globus_result_t                     result,
    globus_xio_iovec_t *                iovec,
    int                                 count,
    globus_size_t                       nbytes,
    globus_xio_data_descriptor_t        data_desc,
    void *                              user_arg);

static
void
gfork_l_read_header_cb(
    globus_xio_handle_t                 handle,
    globus_result_t                     result,
    globus_byte_t *                     buffer,
    globus_size_t                       len,
    globus_size_t                       nbytes,
    globus_xio_data_descriptor_t        data_desc,
    void *                              user_arg);

static
void
gfork_new_child(
    gfork_i_handle_t *                  gfork_handle,
    globus_xio_system_socket_t          socket_handle,
    int                                 read_fd,
    int                                 write_fd,
    globus_list_t *                     list);

static
void
gfork_l_write(
    gfork_i_child_handle_t *            to_kid);

void
gfork_log(
    int                                 level,
    char *                              fmt,
    ...)
{
    va_list                             ap;

    if(gfork_l_options.quiet)
    {
        return;
    }
    if(level > gfork_l_options.log_level)
    {
        return;
    }
    va_start(ap, fmt);
    vfprintf(gfork_l_options.log_fptr, fmt, ap);
    va_end(ap);
    fflush(gfork_l_options.log_fptr);
}

static
void
gfork_l_stop_posting(
    globus_result_t                     result)
{

    gfork_log(2, "Stopped accepting new clients.\n");
    gfork_log(2, "Server will shut down when all children terminate.\n");
    if(result != GLOBUS_SUCCESS)
    {
        char * tmp_msg = globus_error_print_friendly(
            globus_error_peek(result));
        gfork_log(2, "Error: %s", tmp_msg);
        free(tmp_msg);
    }

    if(!gfork_l_done)
    {
        gfork_l_done = GLOBUS_TRUE;
        globus_xio_server_register_close(gfork_l_handle.server_xio, NULL, NULL);
    }
    globus_cond_signal(&gfork_l_cond);
}

static
void
gfork_l_kid_read_close_cb(
    globus_xio_handle_t                 handle,
    globus_result_t                     result,
    void *                              user_arg)
{
    globus_list_t *                     list;
    gfork_i_child_handle_t *            kid_handle;
    gfork_i_state_t                     tmp_state;

    kid_handle = (gfork_i_child_handle_t *) user_arg;

    tmp_state = gfork_i_state_next(
        kid_handle->state, GFORK_EVENT_CLOSE_RETURNS);
    kid_handle->state = tmp_state;

    close(kid_handle->write_fd);
    close(kid_handle->read_fd);

    globus_mutex_lock(&gfork_l_mutex);
    {
        list = globus_list_search(gfork_l_pid_list,
                (void *) (intptr_t) kid_handle->pid);
        if(list != NULL)
        {
            globus_list_remove(&gfork_l_pid_list, list);

            if(!kid_handle->dead)
            {
                kill(kid_handle->pid, SIGKILL);
            }
        }
        globus_fifo_destroy(&kid_handle->write_q);
        globus_free(kid_handle);

        globus_cond_signal(&gfork_l_cond);
    }
    globus_mutex_unlock(&gfork_l_mutex);

}


static
void
gfork_l_kid_write_close_cb(
    globus_xio_handle_t                 handle,
    globus_result_t                     result,
    void *                              user_arg)
{
    gfork_i_child_handle_t *            kid_handle;

    kid_handle = (gfork_i_child_handle_t *) user_arg;

    result = globus_xio_register_close(
        kid_handle->read_xio_handle, NULL,
        gfork_l_kid_read_close_cb,
        kid_handle);
    if(result != GLOBUS_SUCCESS)
    {
        gfork_l_kid_read_close_cb(
            kid_handle->read_xio_handle,
            GLOBUS_SUCCESS,
            kid_handle);
    }
}

static void
gfork_l_write_open_cb(
    globus_xio_handle_t                 handle,
    globus_result_t                     result,
    globus_xio_iovec_t *                iovec,
    int                                 count,
    globus_size_t                       nbytes,
    globus_xio_data_descriptor_t        data_desc,
    void *                              user_arg)
{
    int                                 temp_state;
    gfork_i_child_handle_t *            to_kid;
    gfork_i_child_handle_t *            from_kid;
    gfork_i_msg_t *                     msg;

    msg = (gfork_i_msg_t *) user_arg;
    to_kid = msg->to_kid;

    globus_mutex_lock(&gfork_l_mutex);
    {
        /* will post a read on the kid that was jsut opened.  hte 'from_kid'
            caused the open, now we prime read pipe on it */
        from_kid = msg->from_kid;

        /* since callback returned we set the one that was writting to false */
        to_kid->writting = GLOBUS_FALSE;
        if(result != GLOBUS_SUCCESS)
        {
            goto error_param;
        }

        temp_state =
            gfork_i_state_next(to_kid->state, GFORK_EVENT_OPEN_RETURNS);
        to_kid->state = temp_state;

        /* reuse the msg, must chane the from_kid */
        memset(msg, '\0', sizeof(gfork_i_msg_t));
        msg->from_kid = from_kid;
        gfork_log(1, "posting header read on %d for %d bytes\n",
            msg->from_kid->pid, sizeof(gfork_i_msg_header_t));
        result = globus_xio_register_read(
            from_kid->read_xio_handle,
            (globus_byte_t *)&msg->header,
            sizeof(gfork_i_msg_header_t),
            sizeof(gfork_i_msg_header_t),
            NULL,
            gfork_l_read_header_cb,
            msg);
        if(result != GLOBUS_SUCCESS)
        {
            goto error_post;
        }
        gfork_l_write(to_kid);
    }
    globus_mutex_unlock(&gfork_l_mutex);

    return;

error_post:
error_param:

    gfork_log(1, "Error writing to %d.\n", from_kid->pid);
    globus_free(msg);
    /* XXX this is a dead master issue */
    globus_mutex_unlock(&gfork_l_mutex);
}

void
gfork_i_write_open(
    gfork_i_child_handle_t *            kid_handle)
{
    globus_list_t *                     list;
    gfork_i_msg_t *                     msg;

    globus_hashtable_to_list(&gfork_l_master_pid_table, &list);
    while(!globus_list_empty(list))
    {
        gfork_i_child_handle_t * ms_kid;
        ms_kid = (gfork_i_child_handle_t *) globus_list_remove(&list, list);

        msg = (gfork_i_msg_t *) globus_calloc(1, sizeof(gfork_i_msg_t));
        msg->header.type = GLOBUS_GFORK_MSG_OPEN;
        msg->header.from_pid = kid_handle->pid;
        msg->header.to_pid = ms_kid->pid;
        msg->header.size = 0;
        msg->from_kid = kid_handle;
        msg->cb = gfork_l_write_open_cb;

        globus_fifo_enqueue(&ms_kid->write_q, msg);
        gfork_l_write(ms_kid);
    }
}

static void
gfork_l_write_close_cb(
    globus_xio_handle_t                 handle,
    globus_result_t                     result,
    globus_xio_iovec_t *                iovec,
    int                                 count,
    globus_size_t                       nbytes,
    globus_xio_data_descriptor_t        data_desc,
    void *                              user_arg)
{
    gfork_i_child_handle_t *            ms_handle;
    gfork_i_msg_t *                     msg;

    msg = (gfork_i_msg_t *) user_arg;
    ms_handle = msg->to_kid;

    globus_mutex_lock(&gfork_l_mutex);
    {
        ms_handle->writting = GLOBUS_FALSE;
        gfork_l_write(ms_handle);
    }
    globus_mutex_unlock(&gfork_l_mutex);

    globus_free(msg);
}

static
void
gfork_l_kid_write_close_kickout(
    void *                              arg)
{
    gfork_i_child_handle_t *            kid_handle;

    kid_handle = (gfork_i_child_handle_t *) arg;

    gfork_l_kid_write_close_cb(
        kid_handle->write_xio_handle,
        GLOBUS_SUCCESS,
        kid_handle);
}

void
gfork_i_write_close(
    gfork_i_child_handle_t *            kid_handle)
{
    globus_result_t                     result;
    globus_list_t *                     list;
    gfork_i_msg_t *                     msg;

    globus_hashtable_to_list(&gfork_l_master_pid_table, &list);
    while(!globus_list_empty(list))
    {
        gfork_i_child_handle_t * ms_kid;
        ms_kid = (gfork_i_child_handle_t *) globus_list_remove(&list, list);

        msg = (gfork_i_msg_t *) globus_calloc(1, sizeof(gfork_i_msg_t));
        msg->header.type = GLOBUS_GFORK_MSG_CLOSE;
        msg->header.from_pid = kid_handle->pid;
        msg->header.to_pid = ms_kid->pid;
        msg->header.size = 0;
        msg->cb = gfork_l_write_close_cb;
        msg->from_kid = kid_handle;

        globus_fifo_enqueue(&ms_kid->write_q, msg);
        gfork_l_write(ms_kid);
    }

    result = globus_xio_register_close(
        kid_handle->write_xio_handle, NULL,
        gfork_l_kid_write_close_cb,
        kid_handle);
    if(result != GLOBUS_SUCCESS)
    {
        globus_callback_register_oneshot(
            NULL,
            NULL,
            gfork_l_kid_write_close_kickout,
            kid_handle);
    }
}

static
char **
gfork_l_list_to_array(
    globus_list_t *                     list)
{
    int                                 i = 0;
    char **                             argv;

    argv = (char **) calloc(
        globus_list_size(list) + 1, sizeof(char *));
    i = 0;
    for(;
        !globus_list_empty(list);
        list = globus_list_rest(list))
    {
        argv[i] = (char *) globus_list_first(list);
        i++;
    }
    argv[i] = NULL;

    return argv;
}

static
globus_result_t
gfork_l_spawn_master(
    gfork_i_master_program_ent_t *      ms_ent)
{
    char **                             argv;
    char **                             child_env;
    globus_list_t *                     child_list;
    pid_t                               pid;
    int                                 infds[2];
    int                                 outfds[2];
    int                                 rc;
    globus_result_t                     result;
    gfork_i_msg_t *                     msg;
    globus_list_t *                     child_env_list;
    gfork_i_child_handle_t *            master_child_handle = NULL;
    GForkFuncName(gfork_l_spawn_master);

    if(ms_ent->master == NULL)
    {
        gfork_log(1, "There is no master program.\n");
        return GLOBUS_SUCCESS;
    }
    gfork_log(2, "spawn master: %s\n", ms_ent->master);

    rc = pipe(infds);
    if(rc != 0)
    {
        result = GForkErrorErrno(strerror(errno), errno);
        goto error_inpipe;
    }
    rc = pipe(outfds);
    if(rc != 0)
    {
        result = GForkErrorErrno(strerror(errno), errno);
        goto error_outpipe;
    }

    /* gotta do all globus things before the fork */
    child_env_list = gfork_kid_set_keeper_envs(
        ms_ent->master_env, outfds[0], infds[1]);

    child_env = gfork_l_list_to_array(child_env_list);
    child_list = globus_list_copy(ms_ent->master_arg_list);
    globus_list_insert(&child_list, ms_ent->master);
    argv = gfork_l_list_to_array(child_list);

    pid = fork();
    if(pid == 0)
    {
        setuid(ms_ent->master_uid);
        /* child node, set uid and exec */
        close(outfds[1]);
        close(infds[0]);

        environ = child_env;

        nice(ms_ent->master_nice);

        gfork_log(2, "Master Child FDs %s %s\n",
            getenv(GFORK_CHILD_READ_ENV),
            getenv(GFORK_CHILD_WRITE_ENV));

        gfork_log(2, "running master program: %s\n", argv[0]);
        rc = execv(argv[0], argv);

        /* XXX log error */
        gfork_log(1, "Unable to exec program\n");
        exit(rc);
    }
    else if(pid > 0)
    {
        /* clean up argv and child_list and child_env*/
        globus_list_destroy_all(child_env_list, globus_libc_free);
        globus_list_free(child_list);
        free(child_env);
        free(argv);

        master_child_handle = (gfork_i_child_handle_t *)
            globus_calloc(1, sizeof(gfork_i_child_handle_t));

        globus_fifo_init(&master_child_handle->write_q);

        master_child_handle->state = GFORK_STATE_OPEN;
        master_child_handle->master = GLOBUS_TRUE;
        master_child_handle->write_fd = outfds[1];
        result = gfork_i_make_xio_handle(
            &master_child_handle->write_xio_handle, outfds[1]);
        if(result != GLOBUS_SUCCESS)
        {
            goto error_write_convert;
        }

        master_child_handle->read_fd = infds[0];
        result = gfork_i_make_xio_handle(
            &master_child_handle->read_xio_handle, infds[0]);
        if(result != GLOBUS_SUCCESS)
        {
            goto error_read_convert;
        }

        /* post a read */
        close(outfds[0]);
        close(infds[1]);
        master_child_handle->pid = pid;

        globus_list_insert(&gfork_l_pid_list, (void *) (intptr_t) pid);

        msg = (gfork_i_msg_t *) globus_calloc(1, sizeof(gfork_i_msg_t));
        msg->from_kid = master_child_handle;
        gfork_log(1, "posting header read on %d for %d bytes\n",
            msg->from_kid->pid, sizeof(gfork_i_msg_header_t));
        result = globus_xio_register_read(
            master_child_handle->read_xio_handle,
            (globus_byte_t *)&msg->header,
            sizeof(gfork_i_msg_header_t),
            sizeof(gfork_i_msg_header_t),
            NULL,
            gfork_l_read_header_cb,
            msg);
        if(result != GLOBUS_SUCCESS)
        {
            goto error_read_post;
        }

        globus_hashtable_insert(
            &gfork_l_master_pid_table,
            (void*) (intptr_t) pid,
            master_child_handle);
        gfork_log(2, "master is pid %d\n", pid);
    }
    else
    {
        /* XXX log error */
        result =  GForkErrorErrno(strerror(errno), errno);;
        goto error_fork;
    }

    return GLOBUS_SUCCESS;

error_read_post:
error_read_convert:
    globus_xio_close(master_child_handle->write_xio_handle, NULL);
error_write_convert:
    globus_free(master_child_handle);
    master_child_handle = NULL;
error_fork:
    close(outfds[1]);
    close(infds[0]);
    close(outfds[0]);
    close(infds[1]);

error_outpipe:
error_inpipe:

    return result;
}

void
gfork_l_dead_kid(
    pid_t                               child_pid)
{
    int                                 temp_state;
    globus_list_t *                     list;
    gfork_i_child_handle_t *            kid_handle;

    kid_handle = (gfork_i_child_handle_t *)
        globus_hashtable_remove(
            &gfork_l_pid_table, (void *) (intptr_t) child_pid);
    if(kid_handle == NULL)
    {
        /* could be a master */
        kid_handle = (gfork_i_child_handle_t *)
            globus_hashtable_remove(
                &gfork_l_master_pid_table, (void *) (intptr_t) child_pid);
    }

    /* has to be in one or the other */
    globus_assert(kid_handle != NULL);

    kid_handle->dead = GLOBUS_TRUE;

    temp_state = gfork_i_state_next(
        kid_handle->state, GFORK_EVENT_SIGCHILD);
    kid_handle->state = temp_state;

    if(!kid_handle->master)
    {
        gfork_i_write_close(kid_handle);
    }
    else
    {
        gfork_log(2, "Master died! %d\n", child_pid);
    }

    list = globus_list_search(gfork_l_pid_list, (void *) (intptr_t) child_pid);
    globus_assert(list != NULL);

    globus_list_remove(&gfork_l_pid_list, list);
    gfork_log(2, "Cleaned up child %d, list is at %d\n",
        child_pid, globus_list_size(gfork_l_pid_list));

    globus_cond_signal(&gfork_l_cond);
}

static
void
gfork_l_sigchld(
    void *                              user_arg)
{
    int                                 child_pid;
    int                                 child_status;
    globus_bool_t                       dead;

    gfork_log(2, "Sigint child\n");
    globus_mutex_lock(&gfork_l_mutex);
    {
        while((child_pid = waitpid(-1, &child_status, WNOHANG)) > 0)
        {
            dead = GLOBUS_FALSE;
            if(WIFEXITED(child_status))
            {
                /* normal exit */
                dead = GLOBUS_TRUE;
            }
            else if(WIFSIGNALED(child_status))
            {
                /* killed by */
                dead = GLOBUS_TRUE;
            }

            if(dead)
            {
                gfork_l_connection_count--;
                gfork_l_dead_kid(child_pid);

                gfork_log(2, "Child %d completed\n", child_pid);
            }
        }
    }
    globus_mutex_unlock(&gfork_l_mutex);
}

static
void
gfork_l_int_thats_it_cb(
    void *                              user_arg)
{
    exit(1);
}

static
void
gfork_l_int_delay_cb(
    void *                              user_arg)
{
    pid_t                               kid_pid;
    globus_list_t *                     list;
    globus_reltime_t                    delay;

    gfork_log(2, "Sigint delay cb\n");
    globus_mutex_lock(&gfork_l_mutex);
    {
        /* kill the kids */
        list = gfork_l_pid_list;
        while(!globus_list_empty(list))
        {
            kid_pid = (pid_t) (intptr_t) globus_list_first(list);

            list = globus_list_rest(list);

            kill(SIGKILL, kid_pid);
        }
        GlobusTimeReltimeSet(delay, GFORK_WAIT_FOR_KILL, 0);
        globus_callback_register_oneshot(
            NULL,
            &delay,
            gfork_l_int_thats_it_cb,
            NULL);
    }
    globus_mutex_unlock(&gfork_l_mutex);

}

static
void
gfork_l_int(
    void *                              user_arg)
{
    pid_t                               kid_pid;
    globus_list_t *                     list;
    globus_reltime_t                    delay;

    gfork_log(2, "Sigint\n");

    globus_mutex_lock(&gfork_l_mutex);
    {
        gfork_l_stop_posting(GLOBUS_SUCCESS);

        /* kill the kids */
        list = gfork_l_pid_list;
        while(!globus_list_empty(list))
        {
            kid_pid = (pid_t) (intptr_t) globus_list_first(list);

            list = globus_list_rest(list);

            kill(SIGTERM, kid_pid);
        }

        GlobusTimeReltimeSet(delay, GFORK_WAIT_FOR_KILL, 0);
        globus_callback_register_oneshot(
            NULL,
            &delay,
            gfork_l_int_delay_cb,
            NULL);
    }
    globus_mutex_unlock(&gfork_l_mutex);
}


static
void
gfork_l_server_accepted(
    globus_xio_server_t                 server,
    globus_xio_handle_t                 handle,
    gfork_i_handle_t *                  gfork_handle)
{
    globus_result_t                     result;
    pid_t                               pid;
    int                                 infds[2];
    int                                 outfds[2];
    int                                 rc;
    globus_xio_system_socket_t          socket_handle;
    gfork_i_child_handle_t *            kid_handle;
    globus_list_t *                     list;
    globus_list_t *                     child_list;
    GForkFuncName(gfork_l_server_accept_cb);

    globus_hashtable_to_list(&gfork_l_master_pid_table, &list);
    rc = pipe(infds);
    if(rc != 0)
    {
        result = GForkErrorErrno(strerror(errno), errno);
        goto error_inpipe;
    }
    rc = pipe(outfds);
    if(rc != 0)
    {
        result = GForkErrorErrno(strerror(errno), errno);
        goto error_outpipe;
    }

    result = globus_xio_handle_cntl(
        handle,
        gfork_handle->tcp_driver,
        GLOBUS_XIO_TCP_GET_HANDLE,
        &socket_handle);
    if(result != GLOBUS_SUCCESS)
    {
        goto error_getsocket;
    }

    child_list = gfork_kid_set_keeper_envs(
        gfork_l_options.env_list, outfds[0], infds[1]);
    pid = fork();
    if(pid < 0)
    {
        /* error */
        result = GForkErrorErrno(strerror(errno), errno);
        goto error_fork;
    }
    else if(pid == 0)
    {
        int                         read_fd;
        int                         write_fd;

        nice(gfork_l_handle.opts->nice);
        read_fd = outfds[0];
        write_fd = infds[1];
        close(outfds[1]);
        close(infds[0]);

        if(globus_list_empty(list))
        {
            close(outfds[1]);
            close(infds[0]);

            read_fd = -1;
            write_fd = -1;
        }

        gfork_new_child(
            &gfork_l_handle, socket_handle, read_fd, write_fd, child_list);

        /* hsould not return from this, if we do it is an error */
        goto error_fork;
    }
    else
    {
        /* server */
        fcntl(outfds[1], F_SETFD, FD_CLOEXEC);
        fcntl(infds[0], F_SETFD, FD_CLOEXEC);

        close(outfds[0]);
        close(infds[1]);

        close(socket_handle);
        globus_list_insert(&gfork_l_pid_list, (void *) (intptr_t) pid);
        gfork_log(2, "Started child %d\n", pid);

        gfork_l_connection_count++;
    }
    /* i think we dont care when the close happens */
    globus_xio_register_close(handle, NULL, NULL, NULL);

    /* only make a child handle if we have a master */
    {
        kid_handle = (gfork_i_child_handle_t *)
            globus_calloc(1, sizeof(gfork_i_child_handle_t));
        kid_handle->pid = pid;
        kid_handle->whos_my_daddy = gfork_handle;
        kid_handle->write_fd = outfds[1];
        kid_handle->read_fd = infds[0];
        kid_handle->state = GFORK_STATE_OPENING;
        kid_handle->state = gfork_i_state_next(
            GFORK_STATE_NONE, GFORK_EVENT_ACCEPT_CB);

        globus_fifo_init(&kid_handle->write_q);

        result = gfork_i_make_xio_handle(
            &kid_handle->write_xio_handle, kid_handle->write_fd);
        if(result != GLOBUS_SUCCESS)
        {
            char * tmp_s=globus_error_print_friendly(globus_error_get(result));
            gfork_log(1, "write handle make failed %s\n", tmp_s);
            free(tmp_s);
        }
        result = gfork_i_make_xio_handle(
            &kid_handle->read_xio_handle, kid_handle->read_fd);
        if(result != GLOBUS_SUCCESS)
        {
            char * tmp_s=globus_error_print_friendly(globus_error_get(result));
            gfork_log(1, "read handle make failed %s\n", tmp_s);
            free(tmp_s);
        }
        globus_hashtable_insert(
            &gfork_l_pid_table,
            (void *) (intptr_t) pid,
            kid_handle);

        gfork_i_write_open(kid_handle);
    }
    globus_list_free(list);

    return;

error_fork:
    close(socket_handle);
    close(outfds[0]);
    close(outfds[1]);
error_getsocket:
error_outpipe:
    close(infds[0]);
    close(infds[1]);
error_inpipe:
    globus_list_free(list);
    globus_xio_register_close(handle, NULL, NULL, NULL);
    /* log an error */
    return;
}

static
void
gfork_crowded_write_cb(
    globus_xio_handle_t                 handle,
    globus_result_t                     result,
    globus_byte_t *                     buffer,
    globus_size_t                       len,
    globus_size_t                       nbytes,
    globus_xio_data_descriptor_t        data_desc,
    void *                              user_arg)
{
    globus_xio_register_close(handle, NULL, NULL, NULL);
}

static
void
gfork_l_server_accept_cb(
    globus_xio_server_t                 server,
    globus_xio_handle_t                 handle,
    globus_result_t                     result,
    void *                              user_arg)
{
    gfork_i_handle_t *                  gfork_handle;
    char *                              err_msg;
    GForkFuncName(gfork_l_server_accept_cb);

    gfork_handle = (gfork_i_handle_t *) user_arg;

    globus_mutex_lock(&gfork_l_mutex);
    {
        gfork_l_accepting = GLOBUS_FALSE;
        if(result != GLOBUS_SUCCESS)
        {
            goto error_accept;
        }
        if(gfork_accept_allowed())
        {
            gfork_l_server_accepted(server, handle, gfork_handle);
        }
        else
        {
            result = globus_xio_register_write(
                handle,
                (globus_byte_t *)gfork_l_options.crowded_msg,
                gfork_l_options.crowded_msg_len,
                gfork_l_options.crowded_msg_len,
                NULL,
                gfork_crowded_write_cb,
                NULL);
            if(result != GLOBUS_SUCCESS)
            {
                globus_xio_register_close(handle, NULL, NULL, NULL);
            }
        }

        result = globus_xio_server_register_accept(
            gfork_handle->server_xio,
            gfork_l_server_accept_cb,
            gfork_handle);
        if(result != GLOBUS_SUCCESS)
        {
            gfork_l_stop_posting(result);
        }
        else
        {
            gfork_l_accepting = GLOBUS_TRUE;
        }
    }
    globus_mutex_unlock(&gfork_l_mutex);

    return;

error_accept:
    globus_mutex_unlock(&gfork_l_mutex);

    err_msg = globus_error_print_friendly(globus_error_peek(result));
    gfork_log(1, "GFORK has stopped accepting connections: %s\n", err_msg);
    globus_free(err_msg);
    /* log an error */
    return;
}

static
globus_result_t
gfork_init_server()
{
    globus_list_t *                     list;
    gfork_i_options_t *                 gfork_h;
    globus_result_t                     res;

    gfork_h = &gfork_l_options;

    res = globus_xio_server_get_contact_string(
        gfork_l_handle.server_xio, &g_contact_string);
    if(res != GLOBUS_SUCCESS)
    {
        goto error_contact;
    }
    gfork_log(0, "Listening on: %s\n", g_contact_string);
    fprintf(stdout, "Listening on: %s\n", g_contact_string);

    /* start the master program */

    for(list = gfork_h->master_list;
        !globus_list_empty(list);
        list = globus_list_rest(list))
    {
        gfork_i_master_program_ent_t *  ms_ent;

        ms_ent = (gfork_i_master_program_ent_t *) globus_list_first(list);

        res = gfork_l_spawn_master(ms_ent);
        if(res != GLOBUS_SUCCESS)
        {
            char * tmp_s;

            tmp_s = globus_error_print_friendly(globus_error_get(res));
            gfork_log(0, "Could not start master program: %s: %s\n",
                ms_ent->master, tmp_s);
            free(tmp_s);
        }
    }

    res = globus_xio_server_register_accept(
        gfork_l_handle.server_xio,
        gfork_l_server_accept_cb,
        &gfork_l_handle);
    if(res != GLOBUS_SUCCESS)
    {
        goto error_register;
    }
    gfork_l_accepting = GLOBUS_TRUE;

    return GLOBUS_SUCCESS;

error_register:
error_contact:

    return res;
}

/*
 *  post for in child.  Never returns from here
 */
static
void
gfork_new_child(
    gfork_i_handle_t *                  gfork_handle,
    globus_xio_system_socket_t          socket_handle,
    int                                 read_fd,
    int                                 write_fd,
    globus_list_t *                     list)
{
    int                                 rc = 1;
    GlobusGForkFuncName(gfork_new_child);

    gfork_log(1, "starting child %s\n", gfork_handle->server_argv[0]);

    /* set up the state pipe and envs */
/*    new_list = gfork_kid_set_keeper_envs(list, read_fd, write_fd);
 */   environ = gfork_l_list_to_array(list);

    /* dup the incoming socket */
    rc = dup2(socket_handle, STDIN_FILENO);
    if(rc < 0)
    {
        goto error_dupin;
    }
    rc = dup2(socket_handle, STDOUT_FILENO);
    if(rc < 0)
    {
        goto error_dupout;
    }
    close(socket_handle);

    /* start it */
    rc = execv(gfork_handle->server_argv[0], gfork_handle->server_argv);
    /* if we get to here ecxec failed, fall through to error handling */

error_dupout:
error_dupin:
    /* log error */
    exit(rc);
}

static
globus_result_t
gfork_i_opts_unknown(
   globus_options_handle_t              opts_handle,
    void *                              unknown_arg,
    int                                 argc,
    char **                             argv)
{
    return globus_error_put(globus_error_construct_error(
        NULL,
        NULL,
        2,
        __FILE__,
        "gfork_i_opts_unknown",
        __LINE__,
        "Unknown parameter: %s",
        unknown_arg));
}

/******************** IO functions ****************************/
static
void
gfork_l_read_body_cb(
    globus_xio_handle_t                 handle,
    globus_result_t                     result,
    globus_byte_t *                     buffer,
    globus_size_t                       len,
    globus_size_t                       nbytes,
    globus_xio_data_descriptor_t        data_desc,
    void *                              user_arg)
{
    gfork_i_child_handle_t *            from_kid;
    gfork_i_child_handle_t *            to_kid;
    gfork_i_msg_t *                     msg;
    globus_list_t *                     list = NULL;
    gfork_i_msg_t *                     msg2 = NULL;

    /* now we have the message, gotta decide where it goes */
    msg = (gfork_i_msg_t *) user_arg;
    gfork_log(2, "Body read from pid %d\n", msg->from_kid->pid);
    msg->cb = gfork_l_writev_cb;

    gfork_log(2, "gfork_l_read_body_cb\n");
    globus_mutex_lock(&gfork_l_mutex);
    {
        if(result != GLOBUS_SUCCESS)
        {
            goto error_incoming;
        }

        from_kid = msg->from_kid;

        if(msg->header.from_pid != from_kid->pid)
        {
            /* just clean up their mess */
            msg->header.from_pid = from_kid->pid;
        }

        /* if it has a specific to_pid just add it to the guys write queue */
        if(msg->header.to_pid > 0)
        {
            gfork_log(1, "gfork_l_read_body_cb() specific destination\n");
            to_kid = (gfork_i_child_handle_t *) globus_hashtable_lookup(
                &gfork_l_pid_table, (void *) (intptr_t) msg->header.to_pid);
            if(to_kid == NULL)
            {
                /* just cleat in up and repost header */
                globus_free(msg->data);
            }
            else
            {
                msg2 = (gfork_i_msg_t *)
                    globus_malloc(sizeof(gfork_i_msg_t));
                memcpy(msg2, msg, sizeof(gfork_i_msg_t));

                globus_fifo_enqueue(&to_kid->write_q, msg2);
                gfork_l_write(to_kid);
            }
        }
        /* if no specific to_pid behavior depends on master status.
            if negative and not the master it is just a broadcast.
             */
        else if(from_kid->master)
        {

            gfork_log(1, "gfork_l_read_body_cb() from master\n");
            /* if master sends a negitive pid we need to broadcast it */
            globus_hashtable_to_list(&gfork_l_pid_table, &list);

            while(!globus_list_empty(list))
            {
                to_kid = (gfork_i_child_handle_t *)
                    globus_list_remove(&list, list);

                gfork_log(1, "gfork_l_read_body_cb() %d\n", to_kid->pid);
                /* master can exclude a child from broadcast */
                if(msg->header.to_pid != -to_kid->pid)
                {
                    msg2 = (gfork_i_msg_t *)
                        globus_malloc(sizeof(gfork_i_msg_t));
                    memcpy(msg2, msg, sizeof(gfork_i_msg_t));

                    globus_fifo_enqueue(&to_kid->write_q, msg2);
                    gfork_l_write(to_kid);
                }
            }
        }
        else
        {
            gfork_log(1, "gfork_l_read_body_cb() from kid\n");
            /* just have 1 message case, forward to the master */
            msg2 = (gfork_i_msg_t *) globus_malloc(sizeof(gfork_i_msg_t));
            memcpy(msg2, msg, sizeof(gfork_i_msg_t));

            globus_hashtable_to_list(&gfork_l_master_pid_table, &list);
            if(globus_list_empty(list))
            {
                globus_free(msg2);
            }
            for(;
                !globus_list_empty(list);
                list = globus_list_rest(list))
            {
                gfork_i_child_handle_t * ms_kid;
                ms_kid = (gfork_i_child_handle_t *) globus_list_first(list);

                globus_fifo_enqueue(&ms_kid->write_q, msg2);
                gfork_l_write(ms_kid);
            }
        }

        gfork_log(1, "posting header read on %d for %d bytes\n",
            msg->from_kid->pid, sizeof(gfork_i_msg_header_t));
        result = globus_xio_register_read(
            msg->from_kid->read_xio_handle,
            (globus_byte_t *)&msg->header,
            sizeof(gfork_i_msg_header_t),
            sizeof(gfork_i_msg_header_t),
            NULL,
            gfork_l_read_header_cb,
            msg);
        if(result != GLOBUS_SUCCESS)
        {
            goto error_post;
        }
    }
    globus_mutex_unlock(&gfork_l_mutex);

    return;

error_post:
error_incoming:
    globus_mutex_unlock(&gfork_l_mutex);

    globus_free(msg);
    gfork_log(1, "gfork_l_read_body_cb() error\n");
    return;
}

static
void
gfork_l_read_header_cb(
    globus_xio_handle_t                 handle,
    globus_result_t                     result,
    globus_byte_t *                     buffer,
    globus_size_t                       len,
    globus_size_t                       nbytes,
    globus_xio_data_descriptor_t        data_desc,
    void *                              user_arg)
{
    gfork_i_msg_t *                     msg;

    msg = (gfork_i_msg_t *) user_arg;

    gfork_log(2, "Header read from pid %d\n", msg->from_kid->pid);
    if(result != GLOBUS_SUCCESS)
    {
        goto error_incoming;
    }

    switch(msg->header.type)
    {
        case GLOBUS_GFORK_MSG_DATA:
            if(msg->header.size <= 0)
            {
                /* assume a bad message, report header */
                gfork_log(1, "posting header read on %d for %d bytes\n",
                    msg->from_kid->pid, sizeof(gfork_i_msg_header_t));
                result = globus_xio_register_read(
                    msg->from_kid->read_xio_handle,
                    (globus_byte_t *)&msg->header,
                    sizeof(gfork_i_msg_header_t),
                    sizeof(gfork_i_msg_header_t),
                    NULL,
                    gfork_l_read_header_cb,
                    msg);
                if(result != GLOBUS_SUCCESS)
                {
                    goto error_post;
                }
            }
            else
            {
                msg->data = (gfork_i_msg_data_t *) globus_malloc(
                    msg->header.size + sizeof(gfork_i_msg_data_t));
                msg->data->ref = 0;

                gfork_log(1, "posting body read on %d for %d bytes\n",
                    msg->from_kid->pid, msg->header.size);
                result = globus_xio_register_read(
                    msg->from_kid->read_xio_handle,
                    msg->data->buffer,
                    msg->header.size,
                    msg->header.size,
                    NULL,
                    gfork_l_read_body_cb,
                    msg);
                if(result != GLOBUS_SUCCESS)
                {
                    goto error_post;
                }
            }
            break;

        /* any of these we consider garbage */
        case GLOBUS_GFORK_MSG_OPEN:
        case GLOBUS_GFORK_MSG_CLOSE:
        default:
            gfork_log(1, "posting header read on %d for %d bytes\n",
                msg->from_kid->pid, sizeof(gfork_i_msg_header_t));
            result = globus_xio_register_read(
                msg->from_kid->read_xio_handle,
                (globus_byte_t *)&msg->header,
                sizeof(gfork_i_msg_header_t),
                sizeof(gfork_i_msg_header_t),
                NULL,
                gfork_l_read_header_cb,
                msg);
            if(result != GLOBUS_SUCCESS)
            {
                goto error_post;
            }
            break;
    }
    return;

error_incoming:
error_post:
    gfork_log(2, "No read posted for pid %d\n", msg->from_kid->pid);
    globus_free(msg);

    return;
}


static
void
gfork_l_writev_cb(
    globus_xio_handle_t                 handle,
    globus_result_t                     result,
    globus_xio_iovec_t *                iovec,
    int                                 count,
    globus_size_t                       nbytes,
    globus_xio_data_descriptor_t        data_desc,
    void *                              user_arg)
{
    gfork_i_child_handle_t *            to_kid;
    gfork_i_msg_t *                     msg;

    msg = (gfork_i_msg_t *) user_arg;

    globus_mutex_lock(&gfork_l_mutex);
    {
        to_kid = msg->to_kid;
        to_kid->writting = GLOBUS_FALSE;

        msg->data->ref--;
        if(msg->data->ref == 0)
        {
            globus_free(msg->data);
        }
        globus_free(msg);

        if(result != GLOBUS_SUCCESS)
        {
            goto error_incoming;
        }

        gfork_l_write(to_kid);
    }
    globus_mutex_unlock(&gfork_l_mutex);

    return;
error_incoming:
    globus_mutex_unlock(&gfork_l_mutex);

    return;
}

static
void
gfork_l_write(
    gfork_i_child_handle_t *            to_kid)
{
    gfork_i_msg_t *                     msg;
    globus_result_t                     result;
    int                                 iovc = 1;

    if(!to_kid->writting && !globus_fifo_empty(&to_kid->write_q))
    {
        msg = (gfork_i_msg_t *) globus_fifo_dequeue(&to_kid->write_q);

        msg->to_kid = to_kid;
        msg->write_iov[0].iov_base = &msg->header;
        msg->write_iov[0].iov_len = sizeof(gfork_i_msg_header_t);
        if(msg->header.size > 0)
        {
            msg->write_iov[1].iov_base = msg->data->buffer;
            msg->write_iov[1].iov_len = msg->header.size;
            msg->data->ref++;
            iovc++;
        }
        result = globus_xio_register_writev(
            to_kid->write_xio_handle,
            msg->write_iov,
            iovc,
            msg->header.size + sizeof(gfork_i_msg_header_t),
            NULL,
            msg->cb,
            msg);
        assert(msg->cb != NULL);
        if(result != GLOBUS_SUCCESS)
        {
            goto error_register;
        }
        to_kid->writting = GLOBUS_TRUE;
        gfork_log(2, "gfork_l_write() writing to %d\n", to_kid->pid);
    }

    return;

error_register:

    return;
}





/******************** main ****************************/
int
main(
    int                                 argc,
    char **                             argv)
{
    int                                 rc;
    globus_options_handle_t             opt_h;
    globus_result_t                     result = GLOBUS_SUCCESS;

    rc = globus_module_activate(GLOBUS_GFORK_PARENT_MODULE);
    if(rc != 0)
    {
        gfork_l_options.quiet = 0;
        gfork_log(1, "Activation error\n");
        goto error_act;
    }

    gfork_gather_envs();

    globus_hashtable_init(
        &gfork_l_master_pid_table,
        1024,
        globus_hashtable_int_hash,
        globus_hashtable_int_keyeq);

    globus_hashtable_init(
        &gfork_l_pid_table,
        1024,
        globus_hashtable_int_hash,
        globus_hashtable_int_keyeq);
    globus_mutex_init(&gfork_l_mutex, NULL);
    globus_cond_init(&gfork_l_cond, NULL);

    globus_options_init(
        &opt_h, gfork_i_opts_unknown, &gfork_l_options);
    gfork_l_options.log_level = 1;
    gfork_l_options.log_fptr = stdout;
    globus_options_add_table(opt_h, gfork_l_opts_table, &gfork_l_options);

    result = globus_options_command_line_process(opt_h, argc, argv);
    if(result != GLOBUS_SUCCESS)
    {
        gfork_log(1, "Bad command line options\n");
        goto error_opts;
    }

    /* parse out file */
    if(gfork_l_options.conf_file == NULL)
    {
        globus_eval_path("${sysconfdir}/gfork.conf", &gfork_l_options.conf_file);
    }
    result = globus_options_xinetd_file_process(
        opt_h, gfork_l_options.conf_file, "gridftp");
    if(result != GLOBUS_SUCCESS)
    {
        goto error_opts;
    }

    result = globus_i_opts_to_handle(&gfork_l_options, &gfork_l_handle);
    if(result != GLOBUS_SUCCESS)
    {
        goto error_opts;
    }

    /* verify options */
    if(gfork_l_handle.server_argv == NULL)
    {
        gfork_log(1, "You must specify a a program to run\n");
        goto error_opts;
    }

    globus_mutex_lock(&gfork_l_mutex);
    {

        result = globus_callback_register_signal_handler(
            SIGCHLD,
            GLOBUS_TRUE,
            gfork_l_sigchld,
            &gfork_l_options);
        if(result != GLOBUS_SUCCESS)
        {
            gfork_log(1, "Failed to register signal handler\n");
            goto error_signal;
        }

#ifndef BUILD_LITE
/* when threaded add a periodic callback to simulate the SIGCHLD signal, since
 * many versions of LinuxThreads don't seem to pass that to right thread */
    {
        globus_reltime_t                delay;

        GlobusTimeReltimeSet(delay, 10, 0);
        result = globus_callback_register_periodic(
            NULL,
            &delay,
            &delay,
            gfork_l_sigchld,
            &gfork_l_options);
        if(result != GLOBUS_SUCCESS)
        {
            goto error_signal;
        }
    }
#endif

        result = globus_callback_register_signal_handler(
            SIGINT,
            GLOBUS_TRUE,
            gfork_l_int,
            &gfork_l_options);
        if(result != GLOBUS_SUCCESS)
        {
            gfork_log(1, "Failed to register signal handler\n");
            goto error_signal;
        }

        result = gfork_init_server();
        if(result != GLOBUS_SUCCESS)
        {
            gfork_log(1, "Failed to init server\n");
            goto error_server;
        }

        while(!gfork_l_done ||
            !globus_list_empty(gfork_l_pid_list))
        {
            globus_cond_wait(&gfork_l_cond, &gfork_l_mutex);
        }
    }
    globus_mutex_unlock(&gfork_l_mutex);

    gfork_log(1, "Server Done\n");
    return 0;
error_server:
error_signal:
    globus_mutex_unlock(&gfork_l_mutex);
error_opts:
error_act:
    if(result != GLOBUS_SUCCESS)
    {
        char * tmp_msg = globus_error_print_friendly(
            globus_error_peek(result));
        gfork_log(2, "Error: %s", tmp_msg);
        globus_free(tmp_msg);
    }

    gfork_log(1, "Error\n");
    return 1;
}
