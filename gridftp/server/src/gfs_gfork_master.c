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

/* This is ment to be a fairly simple program.  If it starts creeping too
    much we need to reconsider it */

/* TODO: 
   1) memory limitation, perhaps just implemented in brain based on
      connection count?

   2) better load balancing.  maintain a 'whos ready?' list
*/


#include "globus_xio.h"
#include "globus_xio_tcp_driver.h"
#include "globus_xio_gsi.h"
#include "globus_gfork.h"
#include "gfs_i_gfork_plugin.h"
#include "globus_preload.h"

#define GFSGforkError(error_msg, _type)                                     \
    globus_error_put(                                                       \
        globus_error_construct_error(                                       \
            NULL,                                                           \
            NULL,                                                           \
            _type,                                                          \
            __FILE__,                                                       \
            _gfs_gfork_func_name,                                           \
            __LINE__,                                                       \
            "%s",                                                           \
            (error_msg)))

#ifdef __GNUC__
#define GFSGForkFuncName(func) static const char * _gfs_gfork_func_name __attribute__((__unused__)) = #func
#else
#define GFSGForkFuncName(func) static const char * _gfs_gfork_func_name = #func
#endif

#define GFSGforkTopNiceShare(_val) ((_val * 9) / 10) 

#define GFSGforkSizeOfIdleServer        (2*1024*1024)

/* 1/3 of this next value is the minimum TCP buffer size */
#define GFSGforkMinMem                  (1024*512*3)

#define GFS_GFORK_MIN_DELAY             1
#define GFS_GFORK_MAX_DELAY             30
#define GFS_GFORK_MAX_RETRY             2

typedef struct gfs_l_memlimit_entry_s
{
    int                                 mem_size;
    int                                 tcp_buffer_mem;
    pid_t                               pid;
    int                                 count;
    gfork_child_handle_t                handle;
} gfs_l_memlimit_entry_t;

static globus_mutex_t                   g_mutex;
static globus_cond_t                    g_cond;
static globus_bool_t                    g_done = GLOBUS_FALSE;
static globus_xio_server_t              gfs_l_gfork_server_handle;
static globus_fifo_t                    gfs_l_gfork_be_q = NULL;
static int                              g_port = 0;
static int                              g_stripe_count = 0;
static globus_bool_t                    g_use_gsi = GLOBUS_TRUE;
static char *                           g_allowed_dn_file = NULL;
static globus_xio_driver_t              g_tcp_driver;
static globus_xio_driver_t              g_gsi_driver;
static FILE *                           g_log_fptr;
static gfork_child_handle_t             g_handle;
static globus_bool_t                    g_gfork_alive = GLOBUS_FALSE;
static int                              g_connection_count = 0;
static int                              g_connection_count_max_observed = 0;
static globus_hashtable_t               g_gfork_be_table;
static globus_bool_t                    g_backend = GLOBUS_FALSE;
static globus_xio_attr_t                g_attr;
static globus_xio_stack_t               g_stack;
static int                              g_be_timer_sec = (GF_REGISTRATION_TIMEOUT/2);
static char *                           g_reg_cs = NULL;
static char *                           g_be_cs;
static uint32_t                         g_at_once;
static uint32_t                         g_total_cons;

/* memory limiting globals */
static globus_bool_t                    gfs_l_memlimiting = GLOBUS_FALSE;
static globus_off_t                     gfs_l_memlimit_available;
static globus_off_t                     gfs_l_memlimit;
static globus_hashtable_t               gfs_l_memlimit_table;
static int                              gfs_l_memlimit_delay = 1;

/* this minimum makes it so when 1 ends there is enough for the next to start */
static globus_off_t                     gfs_l_memlimit_delay_threshold=GFSGforkSizeOfIdleServer-1;
static int                              gfs_l_memlimit_max_conn = -1;
static int                              gfs_l_max_instance;

static globus_list_t *                  gfs_l_gfork_mem_retry_list = NULL;

static int                              gfs_l_gfork_nice_share_count = 2;

static
globus_result_t
gfs_gfork_master_options(
    int                                 argc,
    char **                             argv);

static
globus_result_t
gfs_l_gfork_read_dynbe(
    globus_xio_handle_t                 handle,
    globus_byte_t *                     buffer,
    globus_size_t                       len);

#define GFS_421_NO_TCP_MEM \
    "421 Not enough memory for TCP buffers.  Try later."

typedef struct gfs_l_gfork_master_entry_s
{
    char *                              table_key;
    globus_callback_handle_t            callback_handle;
    int                                 timeout_count;
    globus_byte_t                       buffer[GF_DYN_PACKET_LEN];
} gfs_l_gfork_master_entry_t;


static
void 
gfs_l_gfork_write_close_cb(
    globus_xio_handle_t                 handle,
    globus_result_t                     result,
    void *                              user_arg)
{
    globus_free(user_arg);
}


static
void
gfs_l_gfork_log(
    globus_result_t                     result,
    int                                 level,
    char *                              fmt,
    ...)
{
    va_list                             ap;

    if(g_log_fptr == NULL)
    {
        return;
    }
    va_start(ap, fmt);

    fprintf(g_log_fptr, "[gridftp gfork plugin] : ");
    if(result != GLOBUS_SUCCESS)
    {
        char * err_str = globus_error_print_friendly(
            globus_error_peek(result));

        fprintf(g_log_fptr, "ERROR : %s : ", err_str);
        globus_free(err_str);
    }
    vfprintf(g_log_fptr, fmt, ap);
    va_end(ap);
    fflush(g_log_fptr);
}

static 
void
gfs_l_gfork_timeout(
    void *                              user_arg)
{
    char *                              buffer;
    gfs_l_gfork_master_entry_t *        ent_buf;

    ent_buf = (gfs_l_gfork_master_entry_t *) user_arg;

    globus_mutex_lock(&g_mutex);
    {
        ent_buf->timeout_count--;
        if(ent_buf->timeout_count < 0)
        {
            buffer = (char *) ent_buf->buffer;
            gfs_l_gfork_log(
                GLOBUS_SUCCESS, 2, "Backend registration for %s expired\n",
                &buffer[GF_DYN_CS_NDX]);
            ent_buf->buffer[GF_VERSION_NDX] = GF_VERSION_TIMEOUT;
            globus_hashtable_remove(
                &g_gfork_be_table, ent_buf->table_key);

            globus_free(ent_buf->table_key);
            globus_free(ent_buf);
        }
    }
    globus_mutex_unlock(&g_mutex);
}

static
void
gfs_l_gfork_write_cb(
    globus_xio_handle_t                 handle,
    globus_result_t                     result,
    globus_byte_t *                     buffer,
    globus_size_t                       len,
    globus_size_t                       nbytes,
    globus_xio_data_descriptor_t        data_desc,
    void *                              user_arg)
{
    globus_xio_register_close(
        handle,
        NULL,
        gfs_l_gfork_write_close_cb,
        buffer);
}

static
globus_result_t
gfs_l_gfork_read_remove_dynbe(
    globus_xio_handle_t                 handle,
    globus_byte_t *                     buffer,
    globus_size_t                       len)
{
    globus_result_t                     result;
    gfs_l_gfork_master_entry_t *        ent_buf;
    globus_bool_t                       ok;
    globus_bool_t                       done;
    int                                 i;
    char *                              table_key;
    GFSGForkFuncName(gfs_l_gfork_read_remove_dynbe);

        /* we are only ok if the string ends in a \0 */
    done = GLOBUS_FALSE;
    ok = GLOBUS_TRUE;
    for(i = GF_DYN_CS_NDX; i < GF_DYN_CS_NDX + GF_DYN_CS_LEN && !done; i++)
    {
        if(buffer[i] == '\0')
        {
            ok = !ok;
            done = GLOBUS_TRUE;
        }
        else if(!isalnum(buffer[i]) && buffer[i] != '.' &&
            buffer[i] != '-' && buffer[i] != ':')
        {
            ok = GLOBUS_FALSE;
            done = GLOBUS_TRUE;
        }
        else
        {
            ok = GLOBUS_FALSE;
        }
    }

    if(!ok)
    {
        gfs_l_gfork_log(
            GLOBUS_SUCCESS, 2, "Registration message not ok\n");
        goto error_cs;
    }

    table_key = (char *)&buffer[GF_DYN_CS_NDX];
    ent_buf = (gfs_l_gfork_master_entry_t *) globus_hashtable_remove(
        &g_gfork_be_table, table_key);
    if(ent_buf == NULL)
    {
        buffer[GF_VERSION_NDX] = GF_VERSION;
        buffer[GF_MSG_TYPE_NDX] = GFS_GFORK_MSG_TYPE_NACK;
        gfs_l_gfork_log(
            GLOBUS_SUCCESS, 2, "Cannot find entry to unregister.\n");
    }
    else
    {
        memset(buffer, '\0', GF_DYN_PACKET_LEN);
        buffer[GF_VERSION_NDX] = GF_VERSION;
        buffer[GF_MSG_TYPE_NDX] = GFS_GFORK_MSG_TYPE_ACK;
    }
    result = globus_xio_register_write(
        handle,
        buffer,
        GF_DYN_PACKET_LEN,
        GF_DYN_PACKET_LEN,
        NULL,
        gfs_l_gfork_write_cb,
        NULL);
    if(result != GLOBUS_SUCCESS)
    {
        globus_xio_register_close(
            handle,
            NULL,
            gfs_l_gfork_write_close_cb,
            buffer);
    }
    return GLOBUS_SUCCESS;

error_cs:
    return GLOBUS_SUCCESS;
}
static
void
gfs_l_gfork_read_cb(
    globus_xio_handle_t                 handle,
    globus_result_t                     result,
    globus_byte_t *                     buffer,
    globus_size_t                       len,
    globus_size_t                       nbytes,
    globus_xio_data_descriptor_t        data_desc,
    void *                              user_arg)
{
    GFSGForkFuncName(gfs_l_gfork_read_cb);

    gfs_l_gfork_log(
        result, 3, "Reading incoming registration message\n");

    globus_mutex_lock(&g_mutex);
    {
        if(result != GLOBUS_SUCCESS)
        {
            goto error;
        }
        /* before addind to list make sure the packet is ok */
        if(buffer[GF_VERSION_NDX] != GF_VERSION)
        {
            goto error_version;
        }

        switch(buffer[GF_MSG_TYPE_NDX])
        {
            case GFS_GFORK_MSG_TYPE_DYNBE:
                result = gfs_l_gfork_read_dynbe(handle, buffer, len);
                break;

            case GFS_GFORK_MSG_TYPE_REMOVE_DYNBE:
                result = gfs_l_gfork_read_remove_dynbe(handle, buffer, len);
                break;

            default:
                result = GFSGforkError("unknown registration command", 0);
                gfs_l_gfork_log(
                    result, 2, "Registration command not found\n");
                break;
        }

        if(result != GLOBUS_SUCCESS)
        {
            goto error_cmd;
        }
    }
    globus_mutex_unlock(&g_mutex);

    return;

error_cmd:
error_version:
    /* reuse the buffer we already have */
    buffer[GF_VERSION_NDX] = GF_VERSION;
    buffer[GF_MSG_TYPE_NDX] = GFS_GFORK_MSG_TYPE_NACK;
    result = globus_xio_register_write(
        handle,
        buffer,
        GF_DYN_PACKET_LEN,
        GF_DYN_PACKET_LEN,
        NULL,
        gfs_l_gfork_write_cb,
        NULL);
    if(result != GLOBUS_SUCCESS)
    {
        globus_xio_register_close(
            handle,
            NULL,
            gfs_l_gfork_write_close_cb,
            buffer);
        globus_free(buffer);
        gfs_l_gfork_log(result, 3, "Write NACK failed.\n");
    }
error:
    gfs_l_gfork_log(
        result, 3, "Reading registration exit it error.\n");
    globus_mutex_unlock(&g_mutex);
}

static
void
gfs_l_gfork_read_dynbe_bc_cb(
    globus_xio_handle_t                 xio_handle,
    globus_result_t                     result,
    globus_xio_iovec_t *                iovec,
    int                                 count,
    globus_size_t                       nbytes,
    globus_xio_data_descriptor_t        data_desc,
    void *                              user_arg)
{
    int                                 i;

    for(i = 0; i < count; i++)
    { 
        globus_free(iovec[i].iov_base);
    }
}

static
globus_result_t
gfs_l_gfork_read_dynbe(
    globus_xio_handle_t                 handle,
    globus_byte_t *                     buffer,
    globus_size_t                       len)
{
    globus_result_t                     result;
    gfs_l_gfork_master_entry_t *        ent_buf;
    globus_xio_iovec_t                  iov[1];
    globus_bool_t                       ok;
    globus_bool_t                       done;
    int                                 i;
    globus_reltime_t                    delay;
    uint32_t                            tmp_32;
    uint32_t                            converted_32;
    char *                              table_key;
    GFSGForkFuncName(gfs_l_gfork_read_dynbe);

    if(!g_gfork_alive)
    {
        result = GFSGforkError("GFork is no longer alive", 0);
        gfs_l_gfork_log(
            GLOBUS_SUCCESS, 1,
            "GFork is no longer a live in gfs_l_gfork_read_dynbe\n");

        return result;
    }
        /* we are only ok if the string ends in a \0 */
    done = GLOBUS_FALSE;
    ok = GLOBUS_TRUE;
    for(i = GF_DYN_CS_NDX; i < GF_DYN_CS_NDX + GF_DYN_CS_LEN && !done; i++)
    {
        if(buffer[i] == '\0')
        {
            ok = !ok;
            done = GLOBUS_TRUE;
        }
        else if(!isalnum(buffer[i]) && buffer[i] != '.' &&
            buffer[i] != '-' && buffer[i] != ':')
        {
            ok = GLOBUS_FALSE;
            done = GLOBUS_TRUE;
        }
        else
        {
            ok = GLOBUS_FALSE;
        }
    }

    /* registering client may not be same byte order but worker child
        will be */
    memcpy(&tmp_32, &buffer[GF_DYN_AT_ONCE_NDX], sizeof(uint32_t));
    converted_32 = ntohl(tmp_32);
    memcpy(&buffer[GF_DYN_AT_ONCE_NDX], &converted_32, sizeof(uint32_t));

    memcpy(&tmp_32, &buffer[GF_DYN_TOTAL_NDX], sizeof(uint32_t));
    converted_32 = ntohl(tmp_32);
    memcpy(&buffer[GF_DYN_TOTAL_NDX], &converted_32, sizeof(uint32_t));

    if(!ok)
    {
        gfs_l_gfork_log(
            GLOBUS_SUCCESS, 2, "Registration message not ok\n");
        result = GFSGforkError("bad contact string", 0);
        goto error_cs;
    }

    GlobusTimeReltimeSet(delay, GF_REGISTRATION_TIMEOUT, 0);
    table_key = (char *)&buffer[GF_DYN_CS_NDX];
    ent_buf = (gfs_l_gfork_master_entry_t *) globus_hashtable_lookup(
        &g_gfork_be_table, table_key);
    if(ent_buf == NULL)
    {
        ent_buf = (gfs_l_gfork_master_entry_t *) 
            globus_calloc(1, sizeof(gfs_l_gfork_master_entry_t));
        memcpy(ent_buf->buffer, buffer, GF_DYN_PACKET_LEN);

        ent_buf->table_key = strdup(table_key);
        globus_hashtable_insert(
            &g_gfork_be_table,
            ent_buf->table_key,
            ent_buf);
        globus_fifo_enqueue(&gfs_l_gfork_be_q, ent_buf);
    }
    memset(buffer, '\0', GF_DYN_PACKET_LEN);

    /* count the timeout callbacks.  For each refresh there will
        be a timeout oneshot.  They all must come back before it is
        considered dead.  this way we gaurentee that the latest
        refresh got at least its fair share of time */
    ent_buf->timeout_count++;
    globus_callback_register_oneshot(
        &ent_buf->callback_handle,
        &delay,
        gfs_l_gfork_timeout,
        ent_buf);

    /* write ack */
    buffer[GF_VERSION_NDX] = GF_VERSION;
    buffer[GF_MSG_TYPE_NDX] = GFS_GFORK_MSG_TYPE_ACK;

    result = globus_xio_register_write(
        handle,
        buffer,
        GF_DYN_PACKET_LEN,
        GF_DYN_PACKET_LEN,
        NULL,
        gfs_l_gfork_write_cb,
        NULL);
    if(result != GLOBUS_SUCCESS)
    {
        globus_xio_register_close(
            handle,
            NULL,
            gfs_l_gfork_write_close_cb,
            buffer);
        globus_free(buffer);
        goto error_cs;   
    }
    iov[0].iov_base = malloc(GF_DYN_PACKET_LEN);
    memcpy(iov[0].iov_base, buffer, GF_DYN_PACKET_LEN);
    iov[0].iov_len = GF_DYN_PACKET_LEN;

    buffer = iov[0].iov_base;
    buffer[GF_MSG_TYPE_NDX] = GFS_GFORK_MSG_TYPE_DYNBE;

    gfs_l_gfork_log(
        GLOBUS_SUCCESS, 2, "Successful registration from: %s\n",
        &buffer[GF_DYN_CS_NDX]);
    /* TODO: keep an "in need" list.  if only 3 were available at
        the time the client asked but wanted 4, send this message,
        otherwise, do not send.

        for now this is fine because all children have knowledge
        of all servers and choose themselves. */
    result = globus_gfork_broadcast(
        g_handle,
        iov,
        1,
        gfs_l_gfork_read_dynbe_bc_cb,
        NULL);
    gfs_l_gfork_log(
        result, 3, "Broadcasted new registration\n");
    return GLOBUS_SUCCESS;

error_cs:
    return result;
}

static
globus_result_t
gfs_l_gfork_dn_ok(
    globus_xio_handle_t                 handle)
{
    FILE *                              fptr;
    gss_name_t                          local;
    gss_name_t                          peer;
    OM_uint32                           min_stat;
    OM_uint32                           maj_stat;
    gss_buffer_desc                     local_buf = GSS_C_EMPTY_BUFFER;
    gss_buffer_desc                     peer_buf = GSS_C_EMPTY_BUFFER;
    char                                line[256];
    globus_result_t                     result;
    globus_bool_t                       found;
    GFSGForkFuncName(gfs_l_gfork_dn_ok);

    /* verify we are ok with the sender */
    result = globus_xio_handle_cntl(
        handle,
        g_gsi_driver,
        GLOBUS_XIO_GSI_GET_PEER_NAME,
        &peer);
    if(result != GLOBUS_SUCCESS)
    {
        goto error_peer;
    }
    maj_stat = gss_display_name(
        &min_stat,
        peer,
        &peer_buf,
        NULL);
    if(maj_stat != GSS_S_COMPLETE)
    {
        goto error_peer;
    }
    /* verify we are ok with the sender */
    result = globus_xio_handle_cntl(
        handle,
        g_gsi_driver,
        GLOBUS_XIO_GSI_GET_LOCAL_NAME,
        &local);
    if(result != GLOBUS_SUCCESS)
    {
        goto error_local;
    }
    maj_stat = gss_display_name(
        &min_stat,
        local,
        &local_buf,
        NULL);
    if(maj_stat != GSS_S_COMPLETE)
    {
        goto error_local;
    }

    /* if no file assume seld */
    if(g_allowed_dn_file == NULL)
    {
        if(memcmp(&local_buf.value, peer_buf.value, peer_buf.length) != 0)
        {
            char *                      tmp_str;

            tmp_str = globus_common_create_string(
                "%s not allowed", peer_buf.value);
            result = GFSGforkError(tmp_str, 0);
            goto error_no_match;
        }
    }
    else
    {
        fptr = fopen(g_allowed_dn_file, "r");
        if(fptr == NULL)
        {
            goto error_fopen;
        }
        found = GLOBUS_FALSE;

        while(fscanf(fptr, "\"%[^\"]\"", line) == 1 && !found)
        {
            if(memcmp(peer_buf.value, line, peer_buf.length) == 0)
            {
                found = GLOBUS_TRUE;
            }
        }
        fclose(fptr);
        if(!found)
        {
            char *                      tmp_str;

            tmp_str = globus_common_create_string(
                "%s not found in file", peer_buf.value);
            result = GFSGforkError(tmp_str, 0);
            free(tmp_str);
            goto error_no_match;
        }
    }
    globus_free(peer_buf.value);
    globus_free(local_buf.value);

    return GLOBUS_SUCCESS;
error_no_match:
error_fopen:
error_local:
error_peer:
    return result;
}

static
void
gfs_l_gfork_open_server_cb(
    globus_xio_handle_t                 handle,
    globus_result_t                     result,
    void *                              user_arg)
{
    globus_byte_t *                     buffer;

    if(result != GLOBUS_SUCCESS)
    {
        goto error_accept;
    }

    buffer = malloc(GF_DYN_PACKET_LEN);
    if(g_use_gsi)
    {
        /* verify we are ok with the sender */
        result = gfs_l_gfork_dn_ok(handle);
        if(result != GLOBUS_SUCCESS)
        {
            gfs_l_gfork_log(result, 2, "DN rejected.\n");
            goto error_not_allowed;
        }
    }

    result = globus_xio_register_read(
        handle,
        buffer,
        GF_DYN_PACKET_LEN,
        GF_DYN_PACKET_LEN,
        NULL,
        gfs_l_gfork_read_cb,
        NULL);
    if(result != GLOBUS_SUCCESS)
    {
        goto error_read;
    }

    return;

error_read:
error_not_allowed:

    buffer[GF_VERSION_NDX] = GF_VERSION;
    buffer[GF_MSG_TYPE_NDX] = GFS_GFORK_MSG_TYPE_NACK;
    result = globus_xio_register_write(
        handle,
        buffer,
        GF_DYN_PACKET_LEN,
        GF_DYN_PACKET_LEN,
        NULL,
        gfs_l_gfork_write_cb,
        NULL);
    if(result != GLOBUS_SUCCESS)
    {
        globus_xio_register_close(
            handle,
            NULL,
            gfs_l_gfork_write_close_cb,
            buffer);
        globus_free(buffer);
        gfs_l_gfork_log(result, 3, "Write NACK failed.\n");
    }
error_accept:
    gfs_l_gfork_log(
        result, 2, "Open server error.\n");
    return;
}

static
void
gfs_l_gfork_add_server_accept_cb(
    globus_xio_server_t                 server,
    globus_xio_handle_t                 handle,
    globus_result_t                     result,
    void *                              user_arg)
{
    globus_xio_attr_t                   attr;

    globus_mutex_lock(&g_mutex);
    {
        if(result != GLOBUS_SUCCESS)
        {
            goto error;
        }
        globus_xio_attr_init(&attr);
        if(g_use_gsi)
        {
            result = globus_xio_attr_cntl(
                attr,
                g_gsi_driver,
                GLOBUS_XIO_GSI_SET_AUTHORIZATION_MODE,
                GLOBUS_XIO_GSI_SELF_AUTHORIZATION);
            if(result != GLOBUS_SUCCESS)
            {
                goto error;
            }
        }
        result = globus_xio_register_open(
            handle,
            NULL,
            attr,
            gfs_l_gfork_open_server_cb,
            NULL);
        if(result != GLOBUS_SUCCESS)
        {
            gfs_l_gfork_log(
                result, 1, "Failed to open\n");
            goto error;
        }
        globus_xio_attr_destroy(attr);

error:
        result = globus_xio_server_register_accept(
            gfs_l_gfork_server_handle,
            gfs_l_gfork_add_server_accept_cb,
            NULL);
        if(result != GLOBUS_SUCCESS)
        {
            gfs_l_gfork_log(
                result, 0, "Failed to accept\n");
        }
        gfs_l_gfork_log(
            result, 3, "Accept callback ending.\n");
    }
    globus_mutex_unlock(&g_mutex);
}

static
globus_result_t
gfs_l_gfork_listen()
{
    char *                              contact_string;
    globus_result_t                     res;
    GFSGForkFuncName(gfs_l_gfork_listen);

    res = globus_xio_server_create(
        &gfs_l_gfork_server_handle, g_attr, g_stack);
    if(res != GLOBUS_SUCCESS)
    {
        goto error_server_create;
    }

    res = globus_xio_server_get_contact_string(
        gfs_l_gfork_server_handle,
        &contact_string);
    if(res != GLOBUS_SUCCESS)
    {
        goto error_server;
    }
    gfs_l_gfork_log(
        GLOBUS_SUCCESS, 0, "Listening on %s\n", contact_string);
    globus_free(contact_string);

    res = globus_xio_server_register_accept(
        gfs_l_gfork_server_handle,
        gfs_l_gfork_add_server_accept_cb,
        NULL);
    if(res != GLOBUS_SUCCESS)
    {
        goto error_accept;
    }

    return GLOBUS_SUCCESS;

error_accept:
error_server:
error_server_create:

    return res;
}


static
void
gfs_l_gfork_dyn_be_open(
    gfork_child_handle_t                handle,
    void *                              user_arg,
    pid_t                               from_pid)
{
    uint32_t                            n32;
    globus_result_t                     result;
    globus_xio_iovec_t *                iov;
    int                                 i = 0;
    int                                 iovc = 0;
    globus_bool_t                       done = GLOBUS_FALSE;
    gfs_l_gfork_master_entry_t *        ent_buf;

    iov = (globus_xio_iovec_t *) globus_calloc(
        globus_fifo_size(&gfs_l_gfork_be_q),
        sizeof(globus_xio_iovec_t));

    while(!done && (i < g_stripe_count || g_stripe_count == 0))
    {
        if(globus_fifo_empty(&gfs_l_gfork_be_q))
        {
            done = GLOBUS_TRUE;
        }
        else
        {
            ent_buf = (gfs_l_gfork_master_entry_t *)
                globus_fifo_dequeue(&gfs_l_gfork_be_q);

            if(ent_buf->buffer[GF_VERSION_NDX] == GF_VERSION_TIMEOUT)
            {
                gfs_l_gfork_log(
                    GLOBUS_SUCCESS, 2, "Freeing timed-out buffer %s\n",
                    &ent_buf->buffer[GF_DYN_CS_NDX]);
                globus_free(ent_buf);
            }
            else
            {
                /* a good buffer */

                /* temparily assign ent_buf here, we ultimatale 
                    want ent_buf-> buffer */
                iov[i].iov_base = ent_buf;
                iov[i].iov_len = GF_DYN_PACKET_LEN;
                i++;
            }
        }
    }
    iovc = i;

    for(i = 0; i < iovc; i++)
    {
        ent_buf = (gfs_l_gfork_master_entry_t *) iov[i].iov_base;
        globus_fifo_enqueue(&gfs_l_gfork_be_q, ent_buf);
        iov[i].iov_base = ent_buf->buffer;

        n32 = iovc - i;
        memcpy(&ent_buf->buffer[GF_DYN_ENTRY_COUNT_NDX],&n32,sizeof(uint32_t));
        gfs_l_gfork_log(
            GLOBUS_SUCCESS, 2, "Re-enqueue %d\n", n32);

        assert(GFS_GFORK_MSG_TYPE_DYNBE == ent_buf->buffer[GF_MSG_TYPE_NDX]);
    }
    /* put them back in */
    if(iovc > 0)
    {
        gfs_l_gfork_log(
            GLOBUS_SUCCESS, 3, "sending to pid %d\n", from_pid);
        result = globus_gfork_send(
            handle,
            from_pid,
            iov,
            iovc,
            NULL,
            NULL);
        if(result != GLOBUS_SUCCESS)
        {
            gfs_l_gfork_log(
                result, 3, "failed to send to %d\n", from_pid);
        }
    }

    globus_free(iov);
}

static
void
gfs_l_gfork_free_write_cb(
    globus_xio_handle_t                 handle,
    globus_result_t                     result,
    globus_xio_iovec_t *                iovec,
    int                                 count,
    globus_size_t                       nbytes,
    globus_xio_data_descriptor_t        data_desc,
    void *                              user_arg)
{
    int                                 i;

    for(i = 0; i < count; i++)
    {
        globus_free(iovec[i].iov_base);
    }
}

static
globus_bool_t
gfs_l_gfork_mem_can_support()
{
    if(gfs_l_memlimit_available-GFSGforkMinMem-GFSGforkSizeOfIdleServer > 0)
    {
        return GLOBUS_TRUE;
    }

    return GLOBUS_FALSE;
}

static
globus_off_t
gfs_l_gfork_mem_limit_calc()
{
    globus_off_t                        mem_given;

    if(gfs_l_memlimit_available <= 0)
    {
        return -1;
    }

    if(g_connection_count <= gfs_l_gfork_nice_share_count)
    {
        mem_given = GFSGforkTopNiceShare(gfs_l_memlimit);
        mem_given = mem_given / gfs_l_gfork_nice_share_count;
    }
    else
    {
        mem_given = gfs_l_memlimit_available / 2;
    }

    /* if not worht giving */
    if(mem_given < GFSGforkMinMem)
    {
        /* gotta kill it */
        return -1;
    }

    if(gfs_l_memlimit_available - mem_given <= 0)
    {
        return -1;
    }

    return mem_given;
}

static
void
gfs_l_gfork_mem_limit_send(
    gfork_child_handle_t                handle,
    pid_t                               to_pid,
    int                                 limit)
{
    uint32_t                            n;
    globus_xio_iovec_t                  iov;
    globus_byte_t *                     buffer;
    globus_result_t                     result;


    n = (uint32_t) limit;
    buffer = globus_malloc(GF_MEM_MSG_LEN);

    buffer[GF_VERSION_NDX] = GF_VERSION;
    buffer[GF_MSG_TYPE_NDX] = GFS_GFORK_MSG_TYPE_MEM;
    memcpy(&buffer[GF_MEM_LIMIT_NDX], &n, sizeof(uint32_t));

    iov.iov_base = buffer;
    iov.iov_len = GF_MEM_MSG_LEN;

    gfs_l_gfork_log(
        GLOBUS_SUCCESS, 3, "sending mem limit %d to %d\n", limit, to_pid);
    result = globus_gfork_send(
        handle,
        to_pid,
        &iov,
        1,
        gfs_l_gfork_free_write_cb,
        NULL);
    if(result != GLOBUS_SUCCESS)
    {
        gfs_l_gfork_log(
            result, 3, "failed to send to %d\n", to_pid);
    }
}

static
void
gfs_l_gfork_kill_send(
    gfork_child_handle_t                handle,
    pid_t                               to_pid,
    const char *                        msg)
{
    globus_xio_iovec_t                  iov;
    globus_byte_t *                     buffer;
    globus_result_t                     result;

    buffer = globus_calloc(1, GF_KILL_MSG_LEN);

    buffer[GF_VERSION_NDX] = GF_VERSION;
    buffer[GF_MSG_TYPE_NDX] = GFS_GFORK_MSG_TYPE_KILL;

    strncpy((char *)&buffer[GF_KILL_STRING_NDX], msg, GF_KILL_STRING_LEN);

    iov.iov_base = buffer;
    iov.iov_len = GF_KILL_MSG_LEN;

    gfs_l_gfork_log(
        GLOBUS_SUCCESS, 3, "sending kill to pid %d\n", to_pid);
    result = globus_gfork_send(
        handle,
        to_pid,
        &iov,
        1,
        gfs_l_gfork_free_write_cb,
        NULL);
    if(result != GLOBUS_SUCCESS)
    {
        gfs_l_gfork_log(
            result, 3, "failed to send kill to %d\n", to_pid);
    }
}

static
void
gfs_l_gfork_kill(
    gfs_l_memlimit_entry_t *            entry)
{
    gfs_l_gfork_log(
        GLOBUS_SUCCESS, 3, "killing pid %d.  mem at: %"
            GLOBUS_OFF_T_FORMAT"\n",
        entry->pid, gfs_l_memlimit_available);
#ifndef TARGET_ARCH_WIN32
    kill(entry->pid, SIGKILL);
#endif
    gfs_l_memlimit_available += entry->mem_size;

    globus_free(entry);
    /* gfs_l_gfork_kill_send(handle, from_pid, GFS_421_NO_TCP_MEM); */
}

static
void
gfs_l_gfork_ready_send(
    gfork_child_handle_t                handle,
    pid_t                               to_pid)
{
    globus_xio_iovec_t                  iov;
    globus_byte_t *                     buffer;
    globus_result_t                     result;

    buffer = globus_calloc(1, GF_READY_MSG_LEN);

    buffer[GF_VERSION_NDX] = GF_VERSION;
    buffer[GF_MSG_TYPE_NDX] = GFS_GFORK_MSG_TYPE_READY;

    iov.iov_base = buffer;
    iov.iov_len = GF_READY_MSG_LEN;

    gfs_l_gfork_log(
        GLOBUS_SUCCESS, 3, "sending ready to pid %d\n", to_pid);
    result = globus_gfork_send(
        handle,
        to_pid,
        &iov,
        1, 
        gfs_l_gfork_free_write_cb,
        NULL);
    if(result != GLOBUS_SUCCESS)
    {
        gfs_l_gfork_log(
            result, 3, "failed to send to %d\n", to_pid);
    }
}
   
static
void
gfs_l_gfork_mem_try(
    void *                              user_arg)
{
    int                                 mem_given;
    gfs_l_memlimit_entry_t *            entry;

    entry = (gfs_l_memlimit_entry_t *) user_arg;

    entry->count--;

    /* if we are using a memory limit */
    mem_given = gfs_l_gfork_mem_limit_calc();
    if(mem_given <= 0 ||
        (entry->count <= 0 && mem_given < gfs_l_memlimit_delay_threshold))
    {
        gfs_l_gfork_kill(entry);
    }
    else if(mem_given < gfs_l_memlimit_delay_threshold)
    {
        gfs_l_gfork_log(
            GLOBUS_SUCCESS, 2, "Delaying ready message\n");

        globus_list_insert(&gfs_l_gfork_mem_retry_list, entry);
/*
        globus_callback_register_oneshot(
            NULL,
            &delay,
            gfs_l_gfork_mem_try_cb,
            entry);
*/
        gfs_l_memlimit_delay++;
        if(gfs_l_memlimit_delay >= GFS_GFORK_MAX_DELAY)
        {
            gfs_l_memlimit_delay = GFS_GFORK_MAX_DELAY;
        }
    }
    else
    {
        gfs_l_memlimit_available -= mem_given;

        entry->mem_size += mem_given;
        entry->tcp_buffer_mem = mem_given / 3;
    
        globus_hashtable_insert(
            &gfs_l_memlimit_table, (void *) (intptr_t) entry->pid, entry);

        gfs_l_gfork_mem_limit_send(
            entry->handle, entry->pid, entry->tcp_buffer_mem);
        gfs_l_gfork_ready_send(entry->handle, entry->pid);
    }
}

static
void
gfs_l_gfork_mem_try_cb(
    void *                              user_arg)
{
    globus_list_t *                     list;

    globus_mutex_lock(&g_mutex);
    {
        list = globus_list_search(gfs_l_gfork_mem_retry_list, user_arg);
        if(list != NULL)
        {
            globus_list_remove(&gfs_l_gfork_mem_retry_list, list);
            gfs_l_gfork_mem_try(user_arg);
        }
    }
    globus_mutex_unlock(&g_mutex);
}

static
void
gfs_l_gfork_open_cb(
    gfork_child_handle_t                handle,
    void *                              user_arg,
    pid_t                               from_pid)
{
    gfs_l_memlimit_entry_t *            entry;

    gfs_l_gfork_log(
        GLOBUS_SUCCESS, 2, "Open called for pid %d\n", from_pid);

    globus_mutex_lock(&g_mutex);
    {
        g_connection_count++;
        if(g_connection_count > g_connection_count_max_observed)
        {
            g_connection_count_max_observed = g_connection_count;
        }

        /* if we are using a memory limit */
        if(gfs_l_memlimiting)
        {
            entry = (gfs_l_memlimit_entry_t *) globus_calloc(
                1, sizeof(gfs_l_memlimit_entry_t));
            entry->handle = handle;
            entry->pid = from_pid;

            if(g_connection_count >= gfs_l_memlimit_max_conn
                && gfs_l_memlimit_max_conn != -1)
            {
                goto error;
            }

            /* if i can support the overhead of just the connection */
            if(gfs_l_gfork_mem_can_support())
            {
                entry->count = GFS_GFORK_MAX_RETRY;
                entry->mem_size = GFSGforkSizeOfIdleServer;
                gfs_l_memlimit_available -= entry->mem_size;
                gfs_l_gfork_mem_try(entry);
            }
            else
            {
                goto error;
            }
        }
        if(!g_backend)
        {
            gfs_l_gfork_dyn_be_open(handle, user_arg, from_pid);
        }
        if(!gfs_l_memlimiting)
        {
            gfs_l_gfork_ready_send(handle, from_pid);
        }
    }
    globus_mutex_unlock(&g_mutex);

    return;

error:
    gfs_l_gfork_kill(entry);
    globus_mutex_unlock(&g_mutex);
}

static
void
gfs_l_gfork_fire_delayed()
{
    gfs_l_memlimit_entry_t *            next_entry;

    /* go through them all, if not enough mem they will be re-queue
       could optimize by stoping when the first one doesn't get fired */
    while(!globus_list_empty(gfs_l_gfork_mem_retry_list))
    {
        next_entry = (gfs_l_memlimit_entry_t *) globus_list_remove(
            &gfs_l_gfork_mem_retry_list, gfs_l_gfork_mem_retry_list);

        gfs_l_gfork_mem_try(next_entry);
    }
}

static
void
gfs_l_gfork_error_cb(
    gfork_child_handle_t                handle,
    void *                              user_arg,
    globus_result_t                     result)
{
    globus_mutex_lock(&g_mutex);
    {
        g_gfork_alive = GLOBUS_FALSE;
        g_done = GLOBUS_TRUE;
        globus_cond_signal(&g_cond);
    }
    globus_mutex_unlock(&g_mutex);
}

/* connection cloesd */
static
void
gfs_l_gfork_closed_cb(
    gfork_child_handle_t                handle,
    void *                              user_arg,
    pid_t                               from_pid)
{
    gfs_l_memlimit_entry_t *            entry;

    globus_mutex_lock(&g_mutex);
    {
        g_connection_count--;
        gfs_l_gfork_log(
            GLOBUS_SUCCESS, 2, "Closed called for pid %d\n", from_pid);

        if(gfs_l_memlimiting)
        {
            /* if we have it as a memory entry */
            entry = (gfs_l_memlimit_entry_t *) globus_hashtable_remove(
                &gfs_l_memlimit_table, (void *) (intptr_t) from_pid);
            if(entry != NULL)
            {
                gfs_l_memlimit_available += entry->mem_size;
                globus_free(entry);
            }
            gfs_l_memlimit_delay--;
            if(gfs_l_memlimit_delay < GFS_GFORK_MIN_DELAY)
            {
                gfs_l_memlimit_delay = GFS_GFORK_MIN_DELAY;
            }

            gfs_l_gfork_fire_delayed();
        }
    }
    globus_mutex_unlock(&g_mutex);
}

static
void
gfs_l_gfork_incoming_cb(
    gfork_child_handle_t                handle,
    void *                              user_arg,
    pid_t                               from_pid,
    globus_byte_t *                     buffer,
    globus_size_t                       len)
{
    uint32_t                            tmp32;
    gfs_l_memlimit_entry_t *            entry;
    int                                 diff;

    globus_mutex_lock(&g_mutex);
    {
        if(buffer[GF_VERSION_NDX] != GF_VERSION)
        {
            gfs_l_gfork_log(GLOBUS_SUCCESS, 0, 
                "Incoming message with bad version, ignoring.");
            goto error;
        }

        entry = (gfs_l_memlimit_entry_t *) globus_hashtable_lookup(
            &gfs_l_memlimit_table, (void *) (intptr_t) from_pid);
        if(entry == NULL)
        {
            gfs_l_gfork_log(GLOBUS_SUCCESS, 0, 
                "Incoming message from unknown pid %d", from_pid);
            goto error;
        }

        switch(buffer[GF_MSG_TYPE_NDX])
        {
            case GFS_GFORK_MSG_TYPE_RELEASE:

                memcpy(&tmp32, 
                    &buffer[GF_RELEASE_COUNT_NDX], GF_RELEASE_COUNT_LEN);
                if(tmp32 > 0)
                {
                    gfs_l_gfork_log(GLOBUS_SUCCESS, 0, 
                        "Pid %d returning %d bytes.\n", from_pid, (int)tmp32);

                    diff = entry->tcp_buffer_mem - tmp32;
                    if(diff > 0)
                    {
                        entry->tcp_buffer_mem = tmp32;
                        diff *= 3;
                        gfs_l_memlimit_available += diff;
                        entry->mem_size -= diff;
                    }
                }
                break;

            default:
                gfs_l_gfork_log(GLOBUS_SUCCESS, 0,
                    "Incoming message with bad type, ignoring.");
                goto error;

                break;
        }
    }
    globus_mutex_unlock(&g_mutex);

    return;

error:
    globus_mutex_unlock(&g_mutex);
    gfs_l_gfork_log(GLOBUS_SUCCESS, 0,
        "Error in incoming message.");
}

static
globus_result_t
gfs_l_gfork_xio_setup()
{
    globus_result_t                     result;

    result = globus_xio_attr_init(&g_attr);
    if(result != GLOBUS_SUCCESS)
    {
        goto error_attr_init;
    }
    result = globus_xio_stack_init(&g_stack, NULL);
    if(result != GLOBUS_SUCCESS)
    {
        goto error_stack_init;
    }

    result = globus_xio_driver_load("tcp", &g_tcp_driver);
    if(result != GLOBUS_SUCCESS)
    {
        goto error_tcp;
    }

    result = globus_xio_stack_push_driver(g_stack, g_tcp_driver);
    if(result != GLOBUS_SUCCESS)
    {
        goto error_tcp_push;
    }
    result = globus_xio_attr_cntl(
        g_attr,
        g_tcp_driver,
        GLOBUS_XIO_TCP_SET_PORT,
        g_port);
    if(result != GLOBUS_SUCCESS)
    {
        goto error_port;
    }

    if(g_use_gsi)
    {
        result = globus_xio_driver_load("gsi", &g_gsi_driver);
        if(result != GLOBUS_SUCCESS)
        {
            goto error_gsi;
        }
        result = globus_xio_stack_push_driver(g_stack, g_gsi_driver);
        if(result != GLOBUS_SUCCESS)
        {
            goto error_gsi_push;
        }
        result = globus_xio_attr_cntl(
            g_attr,
            g_gsi_driver,
            GLOBUS_XIO_GSI_SET_AUTHORIZATION_MODE,
            GLOBUS_XIO_GSI_NO_AUTHORIZATION);
        if(result != GLOBUS_SUCCESS)
        {
            goto error_gsi_push;
        }
    }

    return GLOBUS_SUCCESS;

error_gsi_push:
error_gsi:
error_port:
error_tcp_push:
error_tcp:
error_stack_init:
error_attr_init:

    return result;
}

/* BACKEND LOGIC */
static
void
gfs_l_gfork_backend_xio_read_cb(
    globus_xio_handle_t                 handle,
    globus_result_t                     result,
    globus_byte_t *                     buffer,
    globus_size_t                       len,
    globus_size_t                       nbytes,
    globus_xio_data_descriptor_t        data_desc,
    void *                              user_arg)
{
    if(result != GLOBUS_SUCCESS)
    {
        /* just log it */
        gfs_l_gfork_log(result, 0, "Backend registration failed\n");
    }

    globus_free(buffer);

    globus_xio_register_close(
        handle,
        NULL,
        NULL,
        NULL);
}

static
void
gfs_l_gfork_backend_xio_write_cb(
    globus_xio_handle_t                 handle,
    globus_result_t                     result,
    globus_byte_t *                     buffer,
    globus_size_t                       len,
    globus_size_t                       nbytes,
    globus_xio_data_descriptor_t        data_desc,
    void *                              user_arg)
{

    if(result != GLOBUS_SUCCESS)
    {
        /* just log it */
        gfs_l_gfork_log(result, 0, "Backend registration failed\n");
        goto error;
    }

    result = globus_xio_register_read(
        handle,
        buffer,
        GF_DYN_PACKET_LEN,
        GF_DYN_PACKET_LEN,
        NULL,
        gfs_l_gfork_backend_xio_read_cb,
        NULL);
    if(result != GLOBUS_SUCCESS)
    {
        goto error;
    }
    return;

error:
    globus_free(buffer);

    globus_xio_register_close(
        handle,
        NULL,
        NULL,
        NULL);
}

static
void
gfs_l_gfork_backend_xio_open_cb(
    globus_xio_handle_t                 handle,
    globus_result_t                     result,
    void *                              user_arg)
{
    globus_byte_t *                     buffer;
    uint32_t                            converted_32;

    if(result != GLOBUS_SUCCESS)
    {
        goto error_param;
    }

    buffer = globus_calloc(1, GF_DYN_PACKET_LEN);
    buffer[GF_VERSION_NDX] = GF_VERSION;
    buffer[GF_MSG_TYPE_NDX] = GFS_GFORK_MSG_TYPE_DYNBE;
    converted_32 = htonl(g_at_once);
    memcpy(&buffer[GF_DYN_AT_ONCE_NDX], &converted_32, sizeof(uint32_t));
    converted_32 = htonl(g_total_cons);
    memcpy(&buffer[GF_DYN_TOTAL_NDX], &converted_32, sizeof(uint32_t));
    strncpy((char *)&buffer[GF_DYN_CS_NDX], g_be_cs, GF_DYN_CS_LEN - 1);

    result = globus_xio_register_write(
        handle,
        buffer,
        GF_DYN_PACKET_LEN,
        GF_DYN_PACKET_LEN,
        NULL,
        gfs_l_gfork_backend_xio_write_cb,
        NULL);
    if(result != GLOBUS_SUCCESS)
    {
        goto error_write;
    }

    return;
error_write:
    globus_free(buffer);
error_param:
    gfs_l_gfork_log(result, 0, "Backend registration failed\n");
    globus_xio_register_close(
        handle,
        NULL,
        NULL,
        NULL);
}

static
globus_bool_t
gfs_l_gfork_backend_timeout_true(
    globus_xio_handle_t                 handle,
    globus_xio_operation_type_t         type,
    void *                              user_arg)
{
    return GLOBUS_TRUE;
}


static
void
gfs_l_gfork_backend_timer(
    void *                              user_arg)
{
    globus_result_t                     result;
    globus_xio_handle_t                 xio_handle;
    globus_xio_attr_t                   xio_attr;
    globus_reltime_t                    to;

    gfs_l_gfork_log(GLOBUS_SUCCESS, 0,
        "Backend timer enter\n");

    result = globus_xio_attr_copy(&xio_attr, g_attr);
    globus_assert(result == GLOBUS_SUCCESS);

    GlobusTimeReltimeSet(to, 15, 0);
    globus_xio_attr_cntl(
        xio_attr,
        NULL,
        GLOBUS_XIO_ATTR_SET_TIMEOUT_ALL,
        gfs_l_gfork_backend_timeout_true,
        &to,
        NULL);

    result = globus_xio_handle_create(&xio_handle, g_stack);
    if(result != GLOBUS_SUCCESS)
    {
        /* log nasty error, but don't exit */
        goto error_create;
    }

    gfs_l_gfork_log(GLOBUS_SUCCESS, 0,
        "Attempting to open :%s:\n", g_reg_cs);
    result = globus_xio_register_open(
        xio_handle,
        g_reg_cs,
        xio_attr,
        gfs_l_gfork_backend_xio_open_cb,
        NULL);
    if(result != GLOBUS_SUCCESS)
    {
        /* log nasty error, but don't exit */
        goto error_open;
    }
    globus_xio_attr_destroy(xio_attr);

    return;

error_open:
error_create:
    globus_xio_attr_destroy(xio_attr);
    gfs_l_gfork_log(result, 0, "Backend registration failed\n");
}


static
globus_result_t
gfs_l_gfork_backend_setup()
{
    globus_reltime_t                    period;
    globus_reltime_t                    delay;

    GlobusTimeReltimeSet(delay, 0, 0);
    GlobusTimeReltimeSet(period, g_be_timer_sec, 0);

    gfs_l_gfork_log(GLOBUS_SUCCESS, 1, "Starting timer\n");
    globus_callback_register_periodic(
        NULL,
        &delay,
        &period,
        gfs_l_gfork_backend_timer,
        NULL);

    return GLOBUS_SUCCESS;
}


int
main(
    int                                 argc,
    char **                             argv)
{
    globus_result_t                     result;
    int                                 rc;
    GFSGForkFuncName(main);

    LTDL_SET_PRELOADED_SYMBOLS();

    rc = globus_module_activate(GLOBUS_GFORK_CHILD_MODULE);
    if(rc != 0)
    {
        result = GFSGforkError("Failed to activate gfork", 0);
        goto error_activate;
    }

    result = gfs_gfork_master_options(argc, argv);
    if(result != GLOBUS_SUCCESS)
    {
        goto error_opts;
    }

    if(gfs_l_memlimiting)
    {
        gfs_l_gfork_log(
            GLOBUS_SUCCESS, 1, "Limiting memory usage to %"
                GLOBUS_OFF_T_FORMAT"\n", gfs_l_memlimit_available);
    }
    else
    {
        gfs_l_gfork_log(
            GLOBUS_SUCCESS, 1, "Not limiting memory\n");
    }

    globus_fifo_init(&gfs_l_gfork_be_q);
    globus_mutex_init(&g_mutex, NULL);
    globus_cond_init(&g_cond, NULL);
    g_done = GLOBUS_FALSE;

    globus_hashtable_init(
        &g_gfork_be_table, 
        256,
        globus_hashtable_string_hash,
        globus_hashtable_string_keyeq);

    globus_hashtable_init(
        &gfs_l_memlimit_table,
        256,
        globus_hashtable_int_hash,
        globus_hashtable_int_keyeq);

    globus_mutex_lock(&g_mutex);
    {
        result = gfs_l_gfork_xio_setup();
        if(result != GLOBUS_SUCCESS)
        {
            goto error_xio;
        }

        result = globus_gfork_child_master_start(
            &g_handle,
            NULL,
            gfs_l_gfork_open_cb,
            gfs_l_gfork_closed_cb,
            gfs_l_gfork_incoming_cb,
            gfs_l_gfork_error_cb,
            NULL);
        g_gfork_alive = GLOBUS_TRUE;
        if(result != GLOBUS_SUCCESS)
        {
            goto error_start;
        }

        if(!g_backend)
        {
            result = gfs_l_gfork_listen();
            if(result != GLOBUS_SUCCESS)
            {
                goto error_listen;
            }
        }
        else
        {
            /* start time for registration */
            result = gfs_l_gfork_backend_setup();
            if(result != GLOBUS_SUCCESS)
            {
                goto error_xio;
            }
        }

        while(!g_done)
        {
            globus_cond_wait(&g_cond, &g_mutex);
        }
    }
    globus_mutex_unlock(&g_mutex);

    return 0;

error_listen:
error_start:
error_xio:
    globus_mutex_unlock(&g_mutex);
error_opts:
error_activate:
    gfs_l_gfork_log(result, 0, "\n");

    return 1;
}

static
globus_result_t
gfs_l_gfork_opts_kmgint(
    const char *                        arg,
    globus_off_t *                      out_i)
{
    int                                 i;
    int                                 sc;
    GFSGForkFuncName(gfs_l_gfork_opts_kmgint);

    sc = sscanf(arg, "%d", &i);
    if(sc != 1)
    {
        return GFSGforkError("size is not an integer",
            GFS_GFORK_ERROR_PARAMETER);
    }
    if(strchr(arg, 'K') != NULL)
    {
        *out_i = (globus_off_t)i * 1024;
    }
    else if(strchr(arg, 'M') != NULL)
    {
        *out_i = (globus_off_t)i * 1024 * 1024;
    }
    else if(strchr(arg, 'G') != NULL)
    {
        *out_i = (globus_off_t)i * 1024 * 1024 * 1024;
    }
    else
    {
        *out_i = (globus_off_t)i;
    }

    return GLOBUS_SUCCESS;
}


static
globus_result_t
gfs_l_gfork_opts_help(
    globus_options_handle_t             opts_handle,
    char *                              cmd,
    char **                             opt,
    void *                              arg,
    int *                               out_parms_used)
{
    globus_options_help(opts_handle);

    printf("This program should be executed from gfork only.  It is "
        "not intended to be a stand alone program.\n");
    exit(0);
    return GLOBUS_SUCCESS;
}

static
globus_result_t
gfs_l_gfork_opts_port(
    globus_options_handle_t             opts_handle,
    char *                              cmd,
    char **                             opt,
    void *                              arg,
    int *                               out_parms_used)
{
    globus_result_t                     result;
    int                                 sc;
    int                                 port;
    GFSGForkFuncName(gfs_l_gfork_opts_port);

    sc = sscanf(opt[0], "%d", &port);
    if(sc != 1)
    {
        result = GFSGforkError("port must be an int",
            GFS_GFORK_ERROR_PARAMETER);
        goto error_format;
    }

    g_port = port;
    *out_parms_used = 1;

    return GLOBUS_SUCCESS;
error_format:
    return result;
}

static
globus_result_t
gfs_l_gfork_opts_stripe_count(
    globus_options_handle_t             opts_handle,
    char *                              cmd,
    char **                             opt,
    void *                              arg,
    int *                               out_parms_used)
{
    globus_result_t                     result;
    int                                 sc;
    int                                 stripe_count;
    GFSGForkFuncName(gfs_l_gfork_opts_stripe_count);

    sc = sscanf(opt[0], "%d", &stripe_count);
    if(sc != 1)
    {
        result = GFSGforkError("stripe count must be an int",
            GFS_GFORK_ERROR_PARAMETER);
        goto error_format;
    }

    g_stripe_count = stripe_count;
    *out_parms_used = 1;

    return GLOBUS_SUCCESS;
error_format:
    return result;

}

static
globus_result_t
gfs_l_gfork_opts_gsi(
    globus_options_handle_t             opts_handle,
    char *                              cmd,
    char **                             opt,
    void *                              arg,
    int *                               out_parms_used)
{
    globus_bool_t                       b = GLOBUS_FALSE;

    if(strcasecmp(opt[0], "t") == 0 ||
        strcasecmp(opt[0], "y") == 0 ||
        strcasecmp(opt[0], "yes") == 0 ||
        strcasecmp(opt[0], "true") == 0)
    {
        b = GLOBUS_TRUE;
    }

    g_use_gsi = b;
    *out_parms_used = 1;

    return GLOBUS_SUCCESS;
}

static
globus_result_t
gfs_l_gfork_opts_dn_file(
    globus_options_handle_t             opts_handle,
    char *                              cmd,
    char **                             opt,
    void *                              arg,
    int *                               out_parms_used)
{
    g_allowed_dn_file = opt[0];
    *out_parms_used = 1;

    return GLOBUS_SUCCESS;
}

static
globus_result_t
gfs_l_gfork_opts_reg(
    globus_options_handle_t             opts_handle,
    char *                              cmd,
    char **                             opt,
    void *                              arg,
    int *                               out_parms_used)
{
    g_reg_cs = opt[0];
    *out_parms_used = 1;

    return GLOBUS_SUCCESS;
}

static
globus_result_t
gfs_l_gfork_opts_log(
    globus_options_handle_t             opts_handle,
    char *                              cmd,
    char **                             opt,
    void *                              arg,
    int *                               out_parms_used)
{
    globus_result_t                     result;
    GFSGForkFuncName(gfs_l_gfork_opts_log);

    if(strcmp(opt[0], "-") == 0)
    {
        g_log_fptr = stderr;
    }
    else
    {
        g_log_fptr = fopen(opt[0], "w");
        if(g_log_fptr == NULL)
        {
            result = GFSGforkError("unable to open log file",
                GFS_GFORK_ERROR_PARAMETER);
            goto error_open;
        }
    }
    *out_parms_used = 1;

    return GLOBUS_SUCCESS;
error_open:
    return result;
}

static
globus_result_t
gfs_l_gfork_opts_updatetime(
    globus_options_handle_t             opts_handle,
    char *                              cmd,
    char **                             opt,
    void *                              arg,
    int *                               out_parms_used)
{   
    globus_result_t                     result;
    int                                 sc;
    int                                 tm;
    GFSGForkFuncName(gfs_l_gfork_opts_updatetime);

    sc = sscanf(opt[0], "%d", &tm);
    if(sc != 1)
    {
        result = GFSGforkError("stripe count must be an int",
            GFS_GFORK_ERROR_PARAMETER);
        goto error_format;
    }

    g_be_timer_sec = tm;
    *out_parms_used = 1;

    return GLOBUS_SUCCESS;
error_format:
    return result;

}

static
globus_result_t
gfs_l_gfork_opts_mem_size(
    globus_options_handle_t             opts_handle,
    char *                              cmd,
    char **                             opt,
    void *                              arg,
    int *                               out_parms_used)
{
    globus_result_t                     result;
    globus_off_t                        val = 0;

    result = gfs_l_gfork_opts_kmgint(opt[0], &val);
    if(result != GLOBUS_SUCCESS)
    {
        goto error;
    }

    gfs_l_memlimiting = GLOBUS_TRUE;
    gfs_l_memlimit_available = val;
    gfs_l_memlimit = val;

    return GLOBUS_SUCCESS;
error:
    return result;
}

static
globus_result_t
gfs_l_gfork_opts_mem_limit(
    globus_options_handle_t             opts_handle,
    char *                              cmd,
    char **                             opt,
    void *                              arg,
    int *                               out_parms_used)
{
    globus_result_t                     result;
    globus_off_t                        page_count = -1;
    globus_off_t                        page_size = -1;
    GFSGForkFuncName(gfs_l_gfork_opts_mem_limit);

    gfs_l_memlimiting = GLOBUS_TRUE;

#   if defined(_SC_PHYS_PAGES)
#       if defined(_SC_PAGESIZE)
        {
            page_count = (globus_off_t) sysconf(_SC_PHYS_PAGES);
            page_size =  (globus_off_t) sysconf(_SC_PAGESIZE);
        }
#       endif
#   endif

    if(page_count < 0 || page_size < 0)
    {
        result = GFSGforkError("Cannot automatically determine memory size. "
            "Use the -M option\n",
            GFS_GFORK_ERROR_PARAMETER);
        goto error;
    }

    gfs_l_memlimit_available = (page_count * page_size);

    return GLOBUS_SUCCESS;
error:
    return result;
}


static
globus_result_t
gfs_l_gfork_opts_nice_share(
    globus_options_handle_t             opts_handle,
    char *                              cmd,
    char **                             opt,
    void *                              arg,
    int *                               out_parms_used)
{   
    globus_result_t                     result;
    int                                 sc;
    int                                 n;
    GFSGForkFuncName(gfs_l_gfork_opts_nice_share);

    sc = sscanf(opt[0], "%d", &n);
    if(sc != 1 || n <= 0)
    {
        result = GFSGforkError("nice_share count must be a positive integer",
            GFS_GFORK_ERROR_PARAMETER);
        goto error_format;
    }
    
    gfs_l_gfork_nice_share_count = n;
    *out_parms_used = 1;
    
    return GLOBUS_SUCCESS;
error_format:
    return result;
    
}



globus_options_entry_t                   gfork_l_opts_table[] =
{
    {"help", "h", NULL, NULL,
        "print the help message",
        0, gfs_l_gfork_opts_help},
    {"port", "p", NULL, "<int>",
        "Port where server listens for connections.",
        1, gfs_l_gfork_opts_port},
    {"stripe-count", "s", NULL, "<int>",
        "The max number of stripes to give to each server."
        "  0 is all available.",
        1, gfs_l_gfork_opts_stripe_count},
    {"logfile", "l", NULL, "<path>",
        "Path to the logfile.",
        1, gfs_l_gfork_opts_log},
    {"gsi", "G", NULL, "<bool>",
        "Enable or disable GSI.  Default is on.",
        1, gfs_l_gfork_opts_gsi},
    {"dn-file", "df", NULL, "<path>",
        "Path to a file containing the list of acceptable DNs."
        "  Default is system gridmap file",
        1, gfs_l_gfork_opts_dn_file},
    {"reg-cs", "b", NULL, "<contact string>",
        "Contact to the frontend registry.  This option makes it a data node",
        1, gfs_l_gfork_opts_reg},
    {"update-interval", "u", NULL, "<int>",
        "Number of seconds between registration updates.",
        1, gfs_l_gfork_opts_updatetime},
    {"mem-size", "M", NULL, "<long>",
        "Limit memory usage to a specific value.",
        1, gfs_l_gfork_opts_mem_size},
    {"mem-limit", "m", NULL, "<long>",
        "Limit memory usage.  System will decide how.",
        0, gfs_l_gfork_opts_mem_limit},
    {"nice-share", "ns", NULL, "<int>",
        "Sets the number of connections which will share 90% of the memory."
        "  Default is 2",
        0, gfs_l_gfork_opts_nice_share},
    {NULL, NULL, NULL, NULL, NULL, 0, NULL}
};

static
globus_result_t
gfs_l_gfork_master_opts_unknown(
    globus_options_handle_t             opts_handle,
    void *                              unknown_arg,
    int                                 argc,
    char **                             argv)
{
    return globus_error_put(globus_error_construct_error(
        NULL,
        NULL,
        2,
        __FILE__,
        "gfs_lgfork_master_opts_unknown",
        __LINE__,
        "Unknown parameter: %s",
        unknown_arg));
}



static
globus_result_t
gfs_gfork_master_options(
    int                                 argc,
    char **                             argv)
{
    globus_options_handle_t             opt_h;
    globus_result_t                     result;
    int                                 sc;
    char *                              env_s;
    
    GFSGForkFuncName(gfs_gfork_master_options);

    globus_options_init(
        &opt_h, gfs_l_gfork_master_opts_unknown, NULL);
    globus_options_add_table(opt_h, gfork_l_opts_table, NULL);
    result = globus_options_command_line_process(opt_h, argc, argv);
    if(result != GLOBUS_SUCCESS)
    {
        goto error;
    }
    if(g_reg_cs != NULL)
    {
        g_backend = GLOBUS_TRUE;
    }

    g_be_cs = getenv(GFORK_CHILD_CS_ENV);
    if(g_be_cs == NULL)
    {
        result = GFSGforkError(
            "GFork contact string not set. Was this program forked from GFork?",
            0);
        goto error;
    }

    env_s = getenv(GFORK_CHILD_INSTANCE_ENV);
    if(env_s == NULL)
    {
        result = GFSGforkError(
            "GFork environment: GFORK_CHILD_INSTANCE_ENV not proeprly set."
            "  Was this program sarted from gfork?",
            0);
        goto error;
    }

    /* this is strange, shouldnt happen.   would onyl happen if ran
        master outside of gfork, which shouldnt be done */
    globus_assert(env_s != NULL);

    sc = sscanf(env_s, "%d", &gfs_l_max_instance);
    globus_assert(sc == 1);


    if(gfs_l_memlimiting)
    {
        /* subtract off minimums to maintain */

        /* take off minimum flor */
        gfs_l_memlimit_available -= GFSGforkMinMem;
        /* make sure there is always enough for 1 connection */
        gfs_l_memlimit_available -= GFSGforkSizeOfIdleServer;
        /* determine the maximum number of connections that can be handled
            given the limits */
        gfs_l_memlimit_max_conn =
            gfs_l_memlimit_available / GFSGforkSizeOfIdleServer;

    }

    return GLOBUS_SUCCESS;
error:
    return result;
}

