/*
 * Copyright 1999-2009 University of Chicago
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
 * @file start_socket.c
 * @brief Job Manager Socket Transfer Code
 */
#include "globus_common.h"
#include "globus_gram_job_manager.h"
#include "globus_gsi_credential.h"
#include "globus_xio.h"

#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>

/* If there exists a system which doesn't define the RFC 2292 extensions
 * CMSG_SPACE and CMSG_LEN, add macro definitions in the #ifndef sections
 * below
 */
#ifndef CMSG_SPACE
#error "CMSG_SPACE not defined, unknown padding needed for struct cmsghdr"
#endif

#ifndef CMSG_LEN
#error "CMSG_LEN not defined, unknown data padding needed for struct cmsghdr"
#endif

static const char * globus_l_gatekeeper_env[] = {
                "REMOTE_ADDR", "REQUEST_METHOD", "SCRIPT_NAME",
                "CONTENT_LENGTH", "GATEWAY_INTERFACE", "SSL_CLIENT_CERT",
                NULL };
static
globus_result_t
globus_l_gram_create_handle(
    int                                 sock,
    globus_xio_handle_t *               handle);

static
void
globus_l_gram_startup_socket_callback(
    globus_xio_handle_t                 handle,
    globus_result_t                     result,
    globus_byte_t *                     buffer,
    globus_size_t                       len,
    globus_size_t                       nbytes,
    globus_xio_data_descriptor_t        data_desc,
    void *                              user_arg);

static
int
globus_l_create_starter_connect_socket(
    globus_gram_job_manager_t *         manager,
    int *                               sockptr);

static
int
globus_l_encode_gatekeeper_env(
    struct iovec                       *iov);

static
int
globus_l_decode_gatekeeper_env(
    const char                         *p,
    size_t                              len,
    char                              **remote_addr,
    size_t                             *content_length,
    globus_gsi_cred_handle_t           *cred_handle);

static
int
globus_l_blocking_send_length_and_fds(
    int                                 fd,
    int                                 msg_length,
    int *                               fds,
    int                                 fd_count);

static
int
globus_l_blocking_writev(
    int                                 fd,
    struct iovec *                      iov,
    int                                 iovcnt);

static
int
globus_l_blocking_read(
    int                                 fd,
    void                               *buf,
    int                                 len);

static
void
globus_l_remove_proxy(
    gss_buffer_t                        buffer);

globus_xio_driver_t                     globus_i_gram_job_manager_file_driver;
globus_xio_stack_t                      globus_i_gram_job_manager_file_stack;
#endif

/**
 * Acquire the lock file for the job manager
 *
 * The globus_gram_job_manager_startup_lock() attempts to lock the job
 * manager's lock file. If successful then this job manager must become the
 * main job manager to manage jobs for this particular resource.
 *
 * This function can fail in two main ways: if the lock fails because another
 * process owns a lock, it returns GLOBUS_GRAM_PROTOCOL_ERROR_OLD_JM_ALIVE;
 * otherwise, it returns GLOBUS_GRAM_PROTOCOL_ERROR_LOCKING_STATE_LOCK_FILE. 
 *
 * @param manager
 *     Pointer to job manager state
 * @param lock_fd
 *     Pointer to set to the lock file descriptor.
 *
 * @retval GLOBUS_SUCCESS
 *     Success
 * @retval GLOBUS_GRAM_PROTOCOL_ERROR_LOCKING_STATE_LOCK_FILE
 *     Error locking state file
 * @retval GLOBUS_GRAM_PROTOCOL_ERROR_OLD_JM_ALIVE
 *     Old job manager alive

 */
int
globus_gram_job_manager_startup_lock(
    globus_gram_job_manager_t *         manager,
    int *                               lock_fd)
{
    int                                 rc = 0;
    int                                 lockfd = -1;
    int                                 i;
    int                                 flags;
    enum { GRAM_RETRIES = 100 };

    globus_gram_job_manager_log(
            manager,
            GLOBUS_GRAM_JOB_MANAGER_LOG_DEBUG,
            "event=gram.startup_lock.start "
            "level=DEBUG "
            "path=\"%s\" "
            "\n",
            manager->lock_path);
    
    /* Create and lock lockfile */
    for (i = 0, lockfd = -1; lockfd < 0 && i < GRAM_RETRIES; i++)
    {
        lockfd = open(manager->lock_path, O_RDWR|O_CREAT, S_IRWXU);
    }
    if (lockfd < 0)
    {
        rc = GLOBUS_GRAM_PROTOCOL_ERROR_LOCKING_STATE_LOCK_FILE;
        globus_gram_job_manager_log(
                manager,
                GLOBUS_GRAM_JOB_MANAGER_LOG_ERROR,
                "event=gram.startup_lock.end "
                "level=ERROR "
                "status=%d "
                "path=\"%s\" "
                "msg=\"%s\" "
                "errno=%d "
                "reason=\"%s\"\n",
                -rc,
                manager->lock_path,
                "Error opening state lock file",
                errno,
                strerror(errno));
        goto lockfd_open_failed;
    }
    flags = fcntl(lockfd, F_GETFD);
    if (flags < 0)
    {
        rc = GLOBUS_GRAM_PROTOCOL_ERROR_LOCKING_STATE_LOCK_FILE;
        globus_gram_job_manager_log(
                manager,
                GLOBUS_GRAM_JOB_MANAGER_LOG_ERROR,
                "event=gram.startup_lock.end "
                "level=ERROR "
                "status=%d "
                "path=\"%s\" "
                "msg=\"%s\" "
                "errno=%d "
                "reason=\"%s\"\n",
                -rc,
                manager->lock_path,
                "Error getting file descriptor flags",
                errno,
                strerror(errno));
        goto fcntl_lockfd_failed;
    }
    flags |= FD_CLOEXEC;
    if (fcntl(lockfd, F_SETFD, flags) < 0)
    {
        rc = GLOBUS_GRAM_PROTOCOL_ERROR_LOCKING_STATE_LOCK_FILE;
        globus_gram_job_manager_log(
                manager,
                GLOBUS_GRAM_JOB_MANAGER_LOG_ERROR,
                "event=gram.startup_lock.end "
                "level=ERROR "
                "status=%d "
                "path=\"%s\" "
                "msg=\"%s\" "
                "errno=%d "
                "reason=\"%s\" "
                "\n",
                -rc,
                manager->lock_path,
                "Error setting close-on-exec flag for lock file",
                errno,
                strerror(errno));

        goto fcntl_lockfd_failed;
    }

    rc = globus_gram_job_manager_file_lock(lockfd);
    if (rc != GLOBUS_SUCCESS)
    {
        globus_gram_job_manager_log(
                manager,
                GLOBUS_GRAM_JOB_MANAGER_LOG_DEBUG,
                "event=gram.startup_lock.end "
                "level=DEBUG "
                "status=%d "
                "path=\"%s\" "
                "msg=\"%s\" "
                "errno=%d "
                "reason=\"%s\" "
                "\n",
                -rc,
                manager->lock_path,
                "Unable to lock file, perhaps another job manager is running",
                errno,
                strerror(errno));
        goto lock_failed;
    }

    else
    {
        globus_gram_job_manager_log(
                manager,
                GLOBUS_GRAM_JOB_MANAGER_LOG_DEBUG,
                "event=gram.startup_lock.end "
                "level=DEBUG "
                "path=\"%s\" "
                "status=%d "
                "\n",
                manager->lock_path,
                rc);
    }

    if (rc != GLOBUS_SUCCESS)
    {
lock_failed:
fcntl_lockfd_failed:
        close(lockfd);
        lockfd = -1;
    }
lockfd_open_failed:
    *lock_fd = lockfd;

    return rc;
}
/* globus_gram_job_manager_startup_lock() */

/**
 * Create socket to handle startup messages from other job managers
 *
 * Creates a UNIX domain socket, binds it to a well-known user-and lrm-specific
 * filename, and then creates and registers an XIO handle for select.
 *
 * @param manager
 *     Manager to create the socket for.
 * @param handle
 *     Pointer to XIO handle to be set to the socket descriptor.
 * @param socket_fd
 *     Pointer to file descriptor pointing to the UNIX domain socket.
 */
int
globus_gram_job_manager_startup_socket_init(
    globus_gram_job_manager_t *         manager,
    globus_xio_handle_t *               handle,
    int *                               socket_fd)
{
    static unsigned char                byte[1];
    int                                 sock = -1;
    int                                 rc = 0;
    globus_result_t                     result;
    struct sockaddr_un                  addr;
    mode_t                              old_umask;
    int                                 i;
    int                                 rcvbuf;
    int                                 flags;
    int                                 save_errno;
    FILE *                              fp;
    enum { GRAM_RETRIES = 100 };

    globus_gram_job_manager_log(
            manager,
            GLOBUS_GRAM_JOB_MANAGER_LOG_DEBUG,
            "event=gram.startup_socket_init.start level=DEBUG\n");

    globus_gram_job_manager_log(
            manager,
            GLOBUS_GRAM_JOB_MANAGER_LOG_TRACE,
            "event=gram.startup_socket_init.write_pid.start "
            "level=TRACE "
            "path=\"%s\" "
            "\n",
            manager->pid_path);
    fp = fopen(manager->pid_path, "w");
    if (fp == NULL)
    {
        rc = GLOBUS_GRAM_PROTOCOL_ERROR_WRITING_STATE_FILE;
        globus_gram_job_manager_log(
                manager,
                GLOBUS_GRAM_JOB_MANAGER_LOG_ERROR,
                "event=gram.startup_socket_init.write_pid.end "
                "level=ERROR "
                "path=\"%s\" "
                "status=%d "
                "msg=\"%s\" "
                "errno=%d "
                "reason=\"%s\" "
                "\n",
                manager->pid_path,
                -rc,
                "Error opening pid file",
                errno,
                strerror(errno));

        goto open_pid_failed;
    }

    rc = fprintf(fp, "%ld\n", (long) getpid());
    if (rc < 0)
    {
        rc = GLOBUS_GRAM_PROTOCOL_ERROR_WRITING_STATE_FILE;

        globus_gram_job_manager_log(
                manager,
                GLOBUS_GRAM_JOB_MANAGER_LOG_ERROR,
                "event=gram.startup_socket_init.write_pid.end "
                "level=ERROR "
                "path=\"%s\" "
                "status=%d "
                "msg=\"%s\" "
                "errno=%d "
                "reason=\"%s\" "
                "\n",
                manager->pid_path,
                -rc,
                "Error writing to pid file",
                errno,
                strerror(errno));

        goto write_pid_failed;
    }

    rc = fclose(fp);
    if (rc < 0)
    {
        rc = GLOBUS_GRAM_PROTOCOL_ERROR_WRITING_STATE_FILE;

        globus_gram_job_manager_log(
                manager,
                GLOBUS_GRAM_JOB_MANAGER_LOG_ERROR,
                "event=gram.startup_socket_init.write_pid.end "
                "level=ERROR "
                "path=\"%s\" "
                "status=%d "
                "msg=\"%s\" "
                "errno=%d "
                "reason=\"%s\" "
                "\n",
                manager->pid_path,
                -rc,
                "Error writing to pid file",
                errno,
                strerror(errno));

        goto close_pid_failed;
    }
    fp = NULL;

    globus_gram_job_manager_log(
            manager,
            GLOBUS_GRAM_JOB_MANAGER_LOG_TRACE,
            "event=gram.startup_socket_init.write_pid.end "
            "level=TRACE "
            "path=\"%s\" "
            "status=%d "
            "\n",
            manager->pid_path,
            0);

    /* create and bind socket */
    globus_gram_job_manager_log(
            manager,
            GLOBUS_GRAM_JOB_MANAGER_LOG_TRACE,
            "event=gram.startup_socket_init.create_socket.start "
            "level=TRACE "
            "path=\"%s\" "
            "\n",
            manager->socket_path);
    memset(&addr, 0, sizeof(struct sockaddr_un));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, manager->socket_path, sizeof(addr.sun_path)-1);

    sock = socket(AF_UNIX, SOCK_STREAM, 0);
    if (sock < 0)
    {
        rc = GLOBUS_GRAM_PROTOCOL_ERROR_LOCKING_STATE_LOCK_FILE;

        globus_gram_job_manager_log(
                manager,
                GLOBUS_GRAM_JOB_MANAGER_LOG_ERROR,
                "event=gram.startup_socket_init.create_socket.end "
                "level=ERROR "
                "path=\"%s\" "
                "status=%d "
                "msg=\"%s\" "
                "errno=%d "
                "reason=\"%s\" "
                "\n",
                manager->socket_path,
                -rc,
                "Error creating socket",
                errno,
                strerror(errno));

        goto socket_failed;
    }

    rcvbuf = 10 * GLOBUS_GRAM_PROTOCOL_MAX_MSG_SIZE;
    rc = setsockopt(sock, SOL_SOCKET, SO_RCVBUF, &rcvbuf, sizeof(rcvbuf));
    if (rc < 0 && errno != ENOPROTOOPT)
    {
        rc = GLOBUS_GRAM_PROTOCOL_ERROR_NO_RESOURCES;

        globus_gram_job_manager_log(
                manager,
                GLOBUS_GRAM_JOB_MANAGER_LOG_ERROR,
                "event=gram.startup_socket_init.create_socket.end "
                "level=ERROR "
                "path=\"%s\" "
                "status=%d "
                "msg=\"%s\" "
                "errno=%d "
                "reason=\"%s\" "
                "\n",
                manager->socket_path,
                -rc,
                "Error setting socket buffer size",
                errno,
                strerror(errno));
        goto setsockopt_failed;
    }
    old_umask = umask(S_IRWXG|S_IRWXO);

    (void ) remove(addr.sun_path);
    for (rc = -1, i = 0; rc < 0 && i < GRAM_RETRIES; i++)
    {
        rc = bind(sock, (struct sockaddr *) &addr, sizeof(addr));

        if (rc < 0)
        {
            globus_gram_job_manager_log(
                    manager,
                    GLOBUS_GRAM_JOB_MANAGER_LOG_WARN,
                    "event=gram.startup_socket_init.create_socket "
                    "level=WARN "
                    "path=\"%s\" "
                    "tries=%d "
                    "msg=\"%s\" "
                    "errno=%d "
                    "reason=\"%s\" "
                    "\n",
                    manager->socket_path,
                    i+1,
                    "Error binding socket to filesystem",
                    errno,
                    strerror(errno));
            remove(addr.sun_path);
        }
    }
    save_errno = errno;
    (void) umask(old_umask);
    if (rc < 0)
    {
        rc = GLOBUS_GRAM_PROTOCOL_ERROR_LOCKING_STATE_LOCK_FILE;

        globus_gram_job_manager_log(
                manager,
                GLOBUS_GRAM_JOB_MANAGER_LOG_ERROR,
                "event=gram.startup.socket.create_socket.end "
                "level=ERROR "
                "status=%d "
                "path=\"%s\" "
                "msg=\"%s\" "
                "errno=%d "
                "reason=\"%s\"\n",
                -rc,
                manager->socket_path,
                "Error binding socket to filesystem",
                save_errno,
                strerror(save_errno));

        goto bind_failed;
    }

    flags = fcntl(sock, F_GETFD);
    if (flags < 0)
    {
        rc = GLOBUS_GRAM_PROTOCOL_ERROR_NO_RESOURCES;

        globus_gram_job_manager_log(
                manager,
                GLOBUS_GRAM_JOB_MANAGER_LOG_ERROR,
                "event=gram.startup.socket.create_socket.end "
                "level=ERROR "
                "status=%d "
                "path=\"%s\" "
                "msg=\"%s\" "
                "errno=%d "
                "reason=\"%s\"\n",
                -rc,
                manager->socket_path,
                "Error getting socket flags",
                errno,
                strerror(errno));

        goto fcntl_failed;
    }

    flags |= FD_CLOEXEC;
    if (fcntl(sock, F_SETFD, flags) < 0)
    {
        rc = GLOBUS_GRAM_PROTOCOL_ERROR_NO_RESOURCES;

        globus_gram_job_manager_log(
                manager,
                GLOBUS_GRAM_JOB_MANAGER_LOG_ERROR,
                "event=gram.startup.socket.create_socket.end "
                "level=ERROR "
                "status=%d "
                "path=\"%s\" "
                "msg=\"%s\" "
                "errno=%d "
                "reason=\"%s\"\n",
                -rc,
                manager->socket_path,
                "Error getting socket flags",
                errno,
                strerror(errno));

        goto fcntl_failed;
    }

    listen(sock, 8);

    result = globus_l_gram_create_handle(
            sock,
            handle);

    if (result != GLOBUS_SUCCESS)
    {
        char * errstr;
        char * errstr_escaped;

        errstr = globus_error_print_friendly(globus_error_peek(result));
        errstr_escaped = globus_gram_prepare_log_string(errstr);

        rc = GLOBUS_GRAM_PROTOCOL_ERROR_JM_FAILED_ALLOW_ATTACH;

        globus_gram_job_manager_log(
                manager,
                GLOBUS_GRAM_JOB_MANAGER_LOG_ERROR,
                "event=gram.startup.socket.create_socket.end "
                "level=ERROR "
                "status=%d "
                "path=\"%s\" "
                "msg=\"%s\" "
                "reason=\"%s\"\n",
                -rc,
                manager->socket_path,
                "Error creating xio handle from socket",
                errstr_escaped ? errstr_escaped : "");

        if (errstr)
        {
            free(errstr);
        }
        if (errstr_escaped)
        {
            free(errstr_escaped);
        }
        goto create_handle_failed;
    }

    result = globus_xio_register_read(
            *handle,
            byte,
            0,
            0,
            NULL,
            globus_l_gram_startup_socket_callback,
            manager);
    if (result != GLOBUS_SUCCESS)
    {
        char *                          errstr;
        char *                          errstr_escaped;

        rc = GLOBUS_GRAM_PROTOCOL_ERROR_JM_FAILED_ALLOW_ATTACH;

        errstr = globus_error_print_friendly(globus_error_peek(result));
        errstr_escaped = globus_gram_prepare_log_string(errstr);

        globus_gram_job_manager_log(
                manager,
                GLOBUS_GRAM_JOB_MANAGER_LOG_ERROR,
                "event=gram.startup.socket.create_socket.end "
                "level=ERROR "
                "status=%d "
                "path=\"%s\" "
                "msg=\"%s\" "
                "reason=\"%s\"\n",
                -rc,
                manager->socket_path,
                "Error registering socket for reading",
                errstr_escaped ? errstr_escaped : "");

        if (errstr)
        {
            free(errstr);
        }
        if (errstr_escaped)
        {
            free(errstr_escaped);
        }

        goto register_read_failed;
    }

    globus_gram_job_manager_log(
            manager,
            GLOBUS_GRAM_JOB_MANAGER_LOG_TRACE,
            "event=gram.startup.socket.create_socket.end "
            "level=TRACE "
            "status=%d "
            "path=\"%s\" "
            "\n",
            0,
            manager->socket_path);

    if (rc != GLOBUS_SUCCESS)
    {
register_read_failed:
        globus_xio_close(*handle, NULL);
        *handle = NULL;
create_handle_failed:
fcntl_failed:
bind_failed:
setsockopt_failed:
        close(sock);
        sock = -1;
socket_failed:
close_pid_failed:
        fp = NULL;
write_pid_failed:
        if (fp != NULL)
        {
            fclose(fp);
        }
        remove(manager->pid_path);
    }
open_pid_failed:

    *socket_fd = sock;

    if (rc == GLOBUS_SUCCESS)
    {
        globus_gram_job_manager_log(
            manager,
            GLOBUS_GRAM_JOB_MANAGER_LOG_DEBUG,
            "event=gram.startup_socket_init.end "
            "level=DEBUG "
            "status=0 "
            "path=\"%s\" "
            "\n",
            manager->socket_path);
    }
    else
    {
        globus_gram_job_manager_log(
            manager,
            GLOBUS_GRAM_JOB_MANAGER_LOG_DEBUG,
            "event=gram.startup_socket_init.end "
            "level=DEBUG "
            "status=%d "
            "reason=\"%s\" "
            "\n",
            -rc,
            globus_gram_protocol_error_string(rc));
    }

    return rc;
}
/* globus_gram_job_manager_startup_socket_init() */

/*
 * This is the for job managers running on system where
 * gss_export_sec_context() works. In those cases, the
 * gatekeeper will exit after sending the context and http
 * message and expect the jobmanager to import the context and
 * wrap its replies. For systems without that, the v2 function
 * below is used.
 */
int
globus_gram_job_manager_starter_send(
    globus_gram_job_manager_t *         manager,
    int                                 http_body_fd,
    int                                 context_fd,
    int                                 response_fd,
    gss_cred_id_t                       cred)
{
    int                                 sock;
    char                                byte[1];
    int                                 rc = 0;
    struct iovec                        iov[1];
    int                                 fdarray[3];
    gss_buffer_desc                     cred_buffer;
    OM_uint32                           major_status, minor_status;
    enum { GRAM_RETRIES = 100 };

    globus_gram_job_manager_log(
            manager,
            GLOBUS_GRAM_JOB_MANAGER_LOG_DEBUG,
            "event=gram.send_job.start "
            "level=DEBUG "
            "http_body_fd=%d "
            "context_fd=%d "
            "response_fd=%d "
            "\n",
            http_body_fd,
            context_fd,
            response_fd);

    /* create socket */
    rc = globus_l_create_starter_connect_socket(manager, &sock);
    if (rc != GLOBUS_SUCCESS)
    {
        goto socket_failed;
    }

    /* Export credential to be sent to active job manager */
    major_status = gss_export_cred(
            &minor_status,
            cred,
            GSS_C_NO_OID,
            1,
            &cred_buffer);
    if (GSS_ERROR(major_status))
    {
        rc = GLOBUS_GRAM_PROTOCOL_ERROR_OPENING_USER_PROXY;
        globus_gram_job_manager_log(
                manager,
                GLOBUS_GRAM_JOB_MANAGER_LOG_WARN,
                "event=gram.send_job.end "
                "level=WARN "
                "http_body_fd=%d "
                "context_fd=%d "
                "response_fd=%d "
                "status=%d "
                "reason=\"%s\" "
                "\n",
                http_body_fd,
                context_fd,
                response_fd,
                -rc,
                "Error exporting proxy");
        goto export_cred_failed;
    }

    /* Shouldn't be the case, as we're using the mech-specific 
     * form, which is the path to the proxy on disk
     */
    if (cred_buffer.length > GLOBUS_GRAM_PROTOCOL_MAX_MSG_SIZE)
    {
        rc = GLOBUS_GRAM_PROTOCOL_ERROR_PROTOCOL_FAILED;
        globus_gram_job_manager_log(
                manager,
                GLOBUS_GRAM_JOB_MANAGER_LOG_WARN,
                "event=gram.send_job.end "
                "level=WARN "
                "http_body_fd=%d "
                "context_fd=%d "
                "response_fd=%d "
                "status=%d "
                "reason=\"%s\" "
                "\n",
                http_body_fd,
                context_fd,
                response_fd,
                -rc,
                "Proxy larger than protocol allows");
        goto cred_too_big;
    }

    fdarray[0] = http_body_fd;
    fdarray[1] = context_fd;
    fdarray[2] = response_fd;

    rc = globus_l_blocking_send_length_and_fds(
            sock, (int) cred_buffer.length, fdarray, 3);

    if (rc != GLOBUS_SUCCESS)
    {
        goto sendmsg_failed;
    }

    iov[0].iov_base = cred_buffer.value;
    iov[0].iov_len = cred_buffer.length;

    rc = globus_l_blocking_writev(sock, iov, 1);

    if (rc < cred_buffer.length)
    {
        rc = GLOBUS_GRAM_PROTOCOL_ERROR_PROTOCOL_FAILED;
        globus_gram_job_manager_log(
                manager,
                GLOBUS_GRAM_JOB_MANAGER_LOG_WARN,
                "event=gram.send_job.end "
                "level=WARN "
                "http_body_fd=%d "
                "context_fd=%d "
                "response_fd=%d "
                "status=%d "
                "msg=\"%s\" "
                "errno=%d "
                "reason=\"%s\" "
                "\n",
                http_body_fd,
                context_fd,
                response_fd,
                -rc,
                "Error sending datagram",
                errno,
                strerror(errno));
        goto sendmsg_failed;
    }

    rc = globus_l_blocking_read(sock, byte, 1);

    if (rc <= 0 || byte[0] != 0)
    {
        rc = GLOBUS_GRAM_PROTOCOL_ERROR_PROTOCOL_FAILED;

        globus_gram_job_manager_log(
                manager,
                GLOBUS_GRAM_JOB_MANAGER_LOG_WARN,
                "event=gram.send_job.end "
                "level=WARN "
                "http_body_fd=%d "
                "context_fd=%d "
                "response_fd=%d "
                "status=%d "
                "msg=\"%s\" "
                "errno=%d "
                "reason=\"%s\" "
                "\n",
                http_body_fd,
                context_fd,
                response_fd,
                -rc,
                "Error receiving ack",
                errno,
                strerror(errno));
    }
    else
    {
        rc = GLOBUS_SUCCESS;
    }
    iov[0].iov_base = &byte;
    iov[0].iov_len = 1;
    byte[0]++;
    rc = globus_l_blocking_writev(sock, iov, 1);
    if (rc != 1)
    {
        rc = GLOBUS_GRAM_PROTOCOL_ERROR_PROTOCOL_FAILED;
        globus_gram_job_manager_log(
                manager,
                GLOBUS_GRAM_JOB_MANAGER_LOG_WARN,
                "event=gram.send_job.end "
                "level=WARN "
                "http_body_fd=%d "
                "context_fd=%d "
                "response_fd=%d "
                "status=%d "
                "msg=\"%s\" "
                "errno=%d "
                "reason=\"%s\" "
                "\n",
                http_body_fd,
                context_fd,
                response_fd,
                -rc,
                "Error sending ack",
                errno,
                strerror(errno));
    }
    else
    {
        rc = GLOBUS_SUCCESS;
        globus_gram_job_manager_log(
                manager,
                GLOBUS_GRAM_JOB_MANAGER_LOG_DEBUG,
                "event=gram.send_job.end "
                "level=DEBUG "
                "http_body_fd=%d "
                "context_fd=%d "
                "response_fd=%d "
                "status=%d "
                "\n",
                http_body_fd,
                context_fd,
                response_fd,
                0);
    }

sendmsg_failed:
cred_too_big:
    if (rc != GLOBUS_SUCCESS)
    {
        globus_l_remove_proxy(&cred_buffer);
    }
    
    gss_release_buffer(
            &minor_status,
            &cred_buffer);
export_cred_failed:
    close(sock);
socket_failed:

    return rc;
}
/* globus_gram_job_manager_starter_send() */

int
globus_gram_job_manager_starter_send_v2(
    globus_gram_job_manager_t *         manager,
    gss_cred_id_t                       cred)
{
    int                                 sock;
    char                                byte[1];
    int                                 rc = 0;
    int                                 fdarray[2];
    gss_buffer_desc                     cred_buffer;
    OM_uint32                           major_status, minor_status;
    int                                 msg_total;
    char *                              content_length_str;
    size_t                              content_length;
    unsigned char                       cred_length_buffer[4];
    unsigned char                       env_length_buffer[4];
    struct iovec                        iov[5];
    char *                              cgi_env;
    enum { GRAM_RETRIES = 100 };

    globus_gram_job_manager_log(
            manager,
            GLOBUS_GRAM_JOB_MANAGER_LOG_DEBUG,
            "event=gram.send_job.start "
            "level=DEBUG "
            "\n");

    content_length_str = getenv("CONTENT_LENGTH");
    sscanf(content_length_str, "%zu", &content_length);

    rc = globus_l_create_starter_connect_socket(manager, &sock);
    if (rc != GLOBUS_SUCCESS)
    {
        goto socket_failed;
    }

    /* Export credential to be sent to active job manager */
    major_status = gss_export_cred(
            &minor_status,
            cred,
            GSS_C_NO_OID,
            1,
            &cred_buffer);
    if (GSS_ERROR(major_status))
    {
        rc = GLOBUS_GRAM_PROTOCOL_ERROR_OPENING_USER_PROXY;
        globus_gram_job_manager_log(
                manager,
                GLOBUS_GRAM_JOB_MANAGER_LOG_WARN,
                "event=gram.send_job.end "
                "level=WARN "
                "status=%d "
                "reason=\"%s\" "
                "\n",
                -rc,
                "Error exporting proxy");
        goto export_cred_failed;
    }

    /* Shouldn't be the case, as we're using the mech-specific 
     * form, which is the path to the proxy on disk
     */
    if (cred_buffer.length > GLOBUS_GRAM_PROTOCOL_MAX_MSG_SIZE)
    {
        rc = GLOBUS_GRAM_PROTOCOL_ERROR_PROTOCOL_FAILED;
        globus_gram_job_manager_log(
                manager,
                GLOBUS_GRAM_JOB_MANAGER_LOG_WARN,
                "event=gram.send_job.end "
                "level=WARN "
                "status=%d "
                "reason=\"%s\" "
                "\n",
                -rc,
                "Proxy larger than protocol allows");
        goto cred_too_big;
    }

    globus_l_encode_gatekeeper_env(&iov[4]);
    cgi_env = iov[4].iov_base;

    /* Message format
     * "msg2"
     * 4 bytes: length of credential in network byte order
     * credential blob
     * 4 bytes: length of environment in network byte order
     * sequence of name=value pairs that form the environment
     * passed from the gatekeeper, separated by \0
     * REMOTE_ADDR=%s
     * REQUEST_METHOD=%s
     * SCRIPT_NAME=%s
     * CONTENT_LENGTH=%s
     * GATEWAY_INTERFACE=%s
     * SSL_CLIENT_CERT=%s
     * SSL_CLIENT_CERT_CHAIN%d=%s
     */
    msg_total = 4 /* "msg2" */
            + 4   /* cred length */ + cred_buffer.length
            + 4   /* env length */ + iov[4].iov_len;

    /* fds from/to gatekeeper's socket to the client */
    fdarray[0] = 0;
    fdarray[1] = 1;
    rc = globus_l_blocking_send_length_and_fds(sock, msg_total, fdarray, 2);

    if (rc != GLOBUS_SUCCESS)
    {
        goto sendmsg_failed;
    }

    iov[0].iov_base = "msg2";
    iov[0].iov_len = 4;
    iov[1].iov_base = cred_length_buffer;
    iov[1].iov_len = 4;
    iov[2].iov_base = cred_buffer.value;
    iov[2].iov_len = cred_buffer.length;
    iov[3].iov_base = env_length_buffer;
    iov[3].iov_len = 4;
    /*iov[4] set above*/

    /* network order 4-byte size of the credential token */
    cred_length_buffer[0] = (cred_buffer.length >> 24) & 0xff;
    cred_length_buffer[1] = (cred_buffer.length >> 16) & 0xff;
    cred_length_buffer[2] = (cred_buffer.length >> 8)  & 0xff;
    cred_length_buffer[3] = (cred_buffer.length)       & 0xff;

    env_length_buffer[0] = (iov[4].iov_len >> 24) & 0xff;
    env_length_buffer[1] = (iov[4].iov_len >> 16) & 0xff;
    env_length_buffer[2] = (iov[4].iov_len >> 8)  & 0xff;
    env_length_buffer[3] = (iov[4].iov_len)       & 0xff;

    rc = globus_l_blocking_writev(sock, iov, 5);

    if (rc < msg_total)
    {
        goto sendmsg_failed;
    }

    rc = globus_l_blocking_read(sock, byte, 1);
    if (rc <= 0 || byte[0] != 0)
    {
        rc = GLOBUS_GRAM_PROTOCOL_ERROR_PROTOCOL_FAILED;

        globus_gram_job_manager_log(
                manager,
                GLOBUS_GRAM_JOB_MANAGER_LOG_WARN,
                "event=gram.send_job.end "
                "level=WARN "
                "status=%d "
                "msg=\"%s\" "
                "errno=%d "
                "reason=\"%s\" "
                "\n",
                -rc,
                "Error receiving ack",
                errno,
                strerror(errno));
        goto ack_failed;
    }
    else
    {
        rc = GLOBUS_SUCCESS;
    }
    iov[0].iov_base = byte;
    iov[0].iov_len = 1;
    byte[0]++;

    rc = globus_l_blocking_writev(sock, iov, 1);
    if (rc < 1)
    {
        rc = GLOBUS_GRAM_PROTOCOL_ERROR_PROTOCOL_FAILED;
        globus_gram_job_manager_log(
                manager,
                GLOBUS_GRAM_JOB_MANAGER_LOG_WARN,
                "event=gram.send_job.end "
                "level=WARN "
                "status=%d "
                "msg=\"%s\" "
                "errno=%d "
                "reason=\"%s\" "
                "\n",
                -rc,
                "Error sending ack",
                errno,
                strerror(errno));
    }
    else
    {
        rc = GLOBUS_SUCCESS;
        globus_gram_job_manager_log(
                manager,
                GLOBUS_GRAM_JOB_MANAGER_LOG_DEBUG,
                "event=gram.send_job.end "
                "level=DEBUG "
                "status=%d "
                "\n",
                0);
    }

ack_failed:
sendmsg_failed:
    if (cgi_env)
    {
        free(cgi_env);
    }
cred_too_big:
    if (rc != GLOBUS_SUCCESS)
    {
        globus_l_remove_proxy(&cred_buffer);
    }

    gss_release_buffer(
            &minor_status,
            &cred_buffer);
export_cred_failed:
    close(sock);
socket_failed:

    return rc;
}
/* globus_gram_job_manager_starter_send_v2() */

/* This is the accept/read handler for the inter-jobmanager communication.
 * It accepts, reads, acks, and checks the ack from the first to make
 * sure that it understands this process is now managing the file descriptors
 * and proxy associated with the job.
 */
static
void
globus_l_gram_startup_socket_callback(
    globus_xio_handle_t                 handle,
    globus_result_t                     result,
    globus_byte_t *                     buffer,
    globus_size_t                       len,
    globus_size_t                       nbytes,
    globus_xio_data_descriptor_t        data_desc,
    void *                              user_arg)
{
    globus_gram_job_manager_t *         manager = user_arg;
    int *                               sent_fds;
    unsigned char                       message_length_buffer[4];
    int                                 message_length;
    struct msghdr                       message;
    unsigned char *                     message_buffer;
    struct iovec                        iov[1];
    gss_buffer_desc                     cred_buffer;
    struct cmsghdr *                    control_message = NULL;
    int                                 http_body_fd = -1;
    int                                 context_fd = -1;
    int                                 response_fd = -1;
    int                                 rc;
    gss_ctx_id_t                        context;
    char *                              contact = NULL;
    int                                 job_state_mask;
    globus_gram_jobmanager_request_t *  request;
    OM_uint32                           major_status, minor_status;
    gss_cred_id_t                       cred;
    char                                byte[1] = {0};
    void *                              cmsgbuf = NULL;
    const int                           MAX_NEW_PER_SELECT = 1;
    int                                 accepted;
    globus_bool_t                       done = GLOBUS_FALSE;
    int                                 tries;
    char *                              old_job_contact = NULL;
    globus_gram_jobmanager_request_t *  old_job_request = NULL;
    globus_bool_t                       version_only = GLOBUS_FALSE;
    static unsigned char                cred_buffer_value[GLOBUS_GRAM_PROTOCOL_MAX_MSG_SIZE+16*1024];
    gss_name_t                          name;
    gss_buffer_desc                     output_name;
    struct linger                       linger;
    char *                              gt3_failure_message = NULL;
    char *                              peername = NULL;
    size_t                              content_length;
    int                                 newsock;
    globus_sockaddr_t                   addr;
    globus_socklen_t                    addrlen = sizeof(addr);
    globus_gsi_cred_handle_t            peer_cred_handle = NULL;

    cred_buffer.value = cred_buffer_value;

    for (accepted = 0; !done && accepted < MAX_NEW_PER_SELECT; accepted++)
    {
        int flags;
        globus_gram_job_manager_log(
                manager,
                GLOBUS_GRAM_JOB_MANAGER_LOG_DEBUG,
                "event=gram.new_request.start "
                "level=DEBUG "
                "fd=%d "
                "\n",
                manager->socket_fd);

        newsock = accept(
                manager->socket_fd,
                (struct sockaddr *) &addr,
                &addrlen);
        if (newsock < 0)
        {
            break;
        }

        flags = fcntl(newsock, F_GETFL);
        flags &= ~O_NONBLOCK;
        fcntl(newsock, F_SETFL, flags);

        /* Don't define cmsgbuf as a static char[]---CMSG_SPACE isn't
         * guaranteed to be a constant expression, and alignment issues can
         * occur with some compilers. 
         */
        if (cmsgbuf == NULL)
        {
            /* Big enough to handle either of the sets of descriptors,
             * three for old gatekeeper (http_body_fd, context_fd, and
             * response_fd) or two for new (in, out)
             */
            cmsgbuf = calloc(1, CMSG_SPACE(3 * sizeof(int)));
        }
        /* First we'll receive the message side + ancillary data, then
         * read into a buffer of that size to get the actual data
         */
        iov[0].iov_base = message_length_buffer;
        iov[0].iov_len = 4;
            
        /* Message metadata */
        message.msg_name = NULL;
        message.msg_namelen = 0;
        message.msg_iov = iov;
        message.msg_iovlen = 1;
        message.msg_control = cmsgbuf;
        message.msg_controllen = CMSG_SPACE(3 * sizeof(int));
        message.msg_flags = 0;

        /* Attempt to receive file descriptors */
        tries = 10;
        while (tries > 0)
        { 
            rc = recvmsg(newsock, &message, 0);
            if (rc <= 0 && (errno == EAGAIN || errno == EINTR))
            {
                tries--;
                globus_libc_usleep(10000);
            }
            else
            {
                break;
            }
        }

        if (rc < 4)
        {
            int level;
            char * levelstr;
            done = GLOBUS_TRUE;

            rc = GLOBUS_GRAM_PROTOCOL_ERROR_PROTOCOL_FAILED;

            if (manager->done)
            {
                level = GLOBUS_GRAM_JOB_MANAGER_LOG_TRACE;
                levelstr = "TRACE";
            }
            else if (accepted == 0)
            {
                level = GLOBUS_GRAM_JOB_MANAGER_LOG_WARN;
                levelstr = "WARN";
            }
            else
            {
                level = GLOBUS_GRAM_JOB_MANAGER_LOG_DEBUG;
                levelstr = "DEBUG";
            }
            globus_gram_job_manager_log(
                    manager,
                    level,
                    "event=gram.new_request.end "
                    "level=%s "
                    "fd=%d "
                    "msg=\"%s\" "
                    "status=%d "
                    "errno=%d "
                    "reason=\"%s\" "
                    "\n",
                    levelstr,
                    newsock,
                    "recvmsg failed",
                    -rc,
                    errno,
                    strerror(errno));

            goto failed_receive;
        }

        for (control_message = CMSG_FIRSTHDR(&message);
             control_message != NULL;
             control_message = CMSG_NXTHDR(&message, control_message))
        {
            if (control_message->cmsg_level == SOL_SOCKET &&
                control_message->cmsg_type == SCM_RIGHTS)
            {
                sent_fds = (int *) CMSG_DATA(control_message);
                break;
            }
        }
        message_length  = message_length_buffer[0] << 24;
        message_length += message_length_buffer[1] << 16;
        message_length += message_length_buffer[2] << 8;
        message_length += message_length_buffer[3];

        message_buffer = malloc(message_length);
        if (message_buffer == NULL)
        {
            rc = GLOBUS_GRAM_PROTOCOL_ERROR_MALLOC_FAILED;
            goto failed_malloc;
        }

        rc = globus_l_blocking_read(newsock, message_buffer, message_length);
        if (rc < message_length)
        {
            rc = GLOBUS_GRAM_PROTOCOL_ERROR_PROTOCOL_FAILED;
            goto failed_read;
        }

        if ((message_length > 4) &&
            (strncmp((char *) message_buffer, "msg2", 4) == 0))
        {
            unsigned char * p = message_buffer + 4;
            int env_length;

            if ((p + 4) > (((unsigned char *)message_buffer) + message_length))
            {
                rc = GLOBUS_GRAM_PROTOCOL_ERROR_PROTOCOL_FAILED;
                goto failed_read;
            }

            cred_buffer.length  = *(p++) << 24;
            cred_buffer.length += *(p++) << 16;
            cred_buffer.length += *(p++) << 8;
            cred_buffer.length += *(p++);

            if ((p + cred_buffer.length) >
                    (((unsigned char *) message_buffer) + message_length))
            {
                rc = GLOBUS_GRAM_PROTOCOL_ERROR_PROTOCOL_FAILED;
                goto failed_read;
            }

            cred_buffer.value = p;
            p += cred_buffer.length;

            if ((p + 4) >
                    (((unsigned char *) message_buffer) + message_length))
            {
                rc = GLOBUS_GRAM_PROTOCOL_ERROR_PROTOCOL_FAILED;
                goto failed_read;
            }

            env_length  = *(p++) << 24;
            env_length += *(p++) << 16;
            env_length += *(p++) << 8;
            env_length += *(p++);

            if ((p + env_length) >
                    (((unsigned char *) message_buffer) + message_length))
            {
                rc = GLOBUS_GRAM_PROTOCOL_ERROR_PROTOCOL_FAILED;
                goto failed_read;
            }

            globus_l_decode_gatekeeper_env(
                    (char*)p,
                    env_length,
                    &peername,
                    &content_length,
                    &peer_cred_handle);

            http_body_fd = sent_fds[0];
            response_fd = sent_fds[1];
        }
        else
        {
            struct stat                     stat;

            cred_buffer.length = message_length;
            cred_buffer.value = message_buffer;

            http_body_fd = sent_fds[0];
            context_fd = sent_fds[1];
            response_fd = sent_fds[2];

            rc = fstat(http_body_fd, &stat);

            if (rc < 0)
            {
                done = GLOBUS_TRUE;

                rc = GLOBUS_GRAM_PROTOCOL_ERROR_PROTOCOL_FAILED;

                globus_gram_job_manager_log(
                        manager,
                        GLOBUS_GRAM_JOB_MANAGER_LOG_ERROR,
                        "event=gram.new_request.end "
                        "level=ERROR "
                        "fd=%d "
                        "status=%d "
                        "msg=\"%s\" "
                        "http_body_fd=%d "
                        "context_fd=%d "
                        "response_fd=%d "
                        "errno=%d "
                        "reason=\"%s\" "
                        "\n",
                        newsock,
                        -rc,
                        "Failed determining message length",
                        http_body_fd,
                        context_fd,
                        response_fd,
                        errno,
                        strerror(errno));

                goto ackfailed;
            }
            content_length = (size_t) stat.st_size;
        }

        linger.l_onoff = 1;
        linger.l_linger = 5;
        setsockopt(
                response_fd,
                SOL_SOCKET,
                SO_LINGER,
                &linger,
                sizeof(linger));

        byte[0] = 0;
        errno = 0;
        do
        {
            rc = write(newsock, byte, 1);

            if (rc < 0 && (errno == EINTR || errno == EAGAIN))
            {
                rc = 0;
            }
            else if (rc < 0)
            {
                break;
            }
        }
        while (rc < 1);

        if (rc < 0)
        {
            done = GLOBUS_TRUE;

            rc = GLOBUS_GRAM_PROTOCOL_ERROR_PROTOCOL_FAILED;

            globus_gram_job_manager_log(
                    manager,
                    GLOBUS_GRAM_JOB_MANAGER_LOG_ERROR,
                    "event=gram.new_request.end "
                    "level=ERROR "
                    "fd=%d "
                    "status=%d "
                    "msg=\"%s\" "
                    "http_body_fd=%d "
                    "context_fd=%d "
                    "response_fd=%d "
                    "errno=%d "
                    "reason=\"%s\" "
                    "\n",
                    newsock,
                    -rc,
                    "Failed sending ack",
                    http_body_fd,
                    context_fd,
                    response_fd,
                    errno,
                    strerror(errno));

            goto ackfailed;
        }

        errno = 0;
        rc = -1;
        tries = 10;
        do
        {
            rc = read(newsock, byte, 1);

            if (rc < 0 && (errno == EINTR || errno == EAGAIN))
            {
                rc = 0;
                tries--;
            }
            else if (rc < 0)
            {
                tries--;
            }
        }
        while (rc < 1 && tries >= 0);

        if (rc < 0 || byte[0] != 1)
        {
            done = GLOBUS_TRUE;

            globus_gram_job_manager_log(
                    manager,
                    GLOBUS_GRAM_JOB_MANAGER_LOG_ERROR,
                    "event=gram.new_request.end "
                    "level=ERROR "
                    "fd=%d "
                    "status=%d "
                    "msg=\"%s\" "
                    "http_body_fd=%d "
                    "context_fd=%d "
                    "response_fd=%d "
                    "errno=%d "
                    "reason=\"%s\" "
                    "rc=%d "
                    "byte=%d "
                    "\n",
                    newsock,
                    -GLOBUS_GRAM_PROTOCOL_ERROR_PROTOCOL_FAILED,
                    "Failed receiving ack",
                    http_body_fd,
                    context_fd,
                    response_fd,
                    errno,
                    strerror(errno),
                    (int) rc,
                    (int) byte[0]);

            rc = GLOBUS_GRAM_PROTOCOL_ERROR_PROTOCOL_FAILED;

            goto ackfailed;
        }

        {
            char * p;
            if (cred_buffer.length > GLOBUS_GRAM_PROTOCOL_MAX_MSG_SIZE)
            {
                rc = GLOBUS_GRAM_PROTOCOL_ERROR_PROTOCOL_FAILED;

                goto ackfailed;
            }
            p = cred_buffer.value;
            *(p + cred_buffer.length) = 0;

            major_status = gss_import_cred(
                    &minor_status,
                    &cred,
                    GSS_C_NO_OID,
                    1,
                    &cred_buffer,
                    0,
                    NULL);
        }

        {
            globus_l_remove_proxy(&cred_buffer);
        }

        if (GSS_ERROR(major_status))
        {
            char *                          errstr;
            char *                          errstr_escaped;

            globus_gss_assist_display_status_str(
                    &errstr,
                    "Import cred failed: ",
                    major_status,
                    minor_status,
                    0);

            errstr_escaped = globus_gram_prepare_log_string(errstr);

            globus_gram_job_manager_log(
                    manager,
                    GLOBUS_GRAM_JOB_MANAGER_LOG_ERROR,
                    "event=gram.new_request.end "
                    "level=ERROR "
                    "fd=%d "
                    "status=%d "
                    "http_body_fd=%d "
                    "context_fd=%d "
                    "response_fd=%d "
                    "msg=\"%s\" "
                    "reason=\"%s\" "
                    "\n",
                    newsock,
                    -rc,
                    http_body_fd,
                    context_fd,
                    response_fd,
                    "Error importing credential",
                    errstr_escaped ? errstr_escaped : "");

            if (errstr)
            {
                free(errstr);
            }

            if (errstr_escaped)
            {
                free(errstr_escaped);
            }
            done = GLOBUS_TRUE;
            goto failed_import_cred;
        }

        /* Determine client address if we don't know it from the
         * gatekeeper
         */
        if (peername == NULL)
        {
            globus_sockaddr_t           peer_address;
            socklen_t                   peer_address_len;

            peer_address_len = sizeof(peer_address);
            rc = getpeername(
                    response_fd,
                    (struct sockaddr *) &peer_address,
                    &peer_address_len);
            if (rc == GLOBUS_SUCCESS)
            {
                rc = globus_libc_addr_to_contact_string(
                        &peer_address,
                        GLOBUS_LIBC_ADDR_NUMERIC,
                        &peername);
            }
        }

        /* Load request data */
        rc = globus_gram_job_manager_request_load(
                manager,
                http_body_fd,
                context_fd,
                cred,
                peername,
                peer_cred_handle,
                content_length,
                &request,
                &context,
                &contact,
                &job_state_mask,
                &old_job_contact,
                &old_job_request,
                &version_only,
                &gt3_failure_message);
        if (rc != GLOBUS_SUCCESS)
        {
            if (rc == GLOBUS_GRAM_PROTOCOL_ERROR_OLD_JM_ALIVE &&
                old_job_request)
            {
                if (old_job_request->two_phase_commit != 0) 
                {
                    /*
                     * Condor-G expects waiting for commit message on restarts.
                     */
                    rc = GLOBUS_GRAM_PROTOCOL_ERROR_WAITING_FOR_COMMIT;
                }
                else if (old_job_request->status ==
                            GLOBUS_GRAM_PROTOCOL_JOB_STATE_FAILED )
                {
                    rc = old_job_request->failure_code;
                }
                else
                {
                    rc = GLOBUS_SUCCESS;
                }
                globus_gram_job_manager_log(
                        manager,
                        GLOBUS_GRAM_JOB_MANAGER_LOG_DEBUG,
                        "event=gram.new_request.info "
                        "level=DEBUG "
                        "gramid=%s "
                        "msg=\"%s\" "
                        "response=%d "
                        "job_state=%d "
                        "job_manager_state=%s "
                        "job_manager_restart_state=%s "
                        "\n",
                        old_job_request->job_contact_path,
                        "Restarting already restarted request",
                        rc,
                        old_job_request->status,
                        globus_i_gram_job_manager_state_strings[
                                old_job_request->jobmanager_state],
                        globus_i_gram_job_manager_state_strings[
                                old_job_request->restart_state]);
            }
            else if(rc != GLOBUS_GRAM_PROTOCOL_ERROR_OLD_JM_ALIVE)
            {
                assert(old_job_request == NULL);
                globus_i_gram_send_job_failure_stats(manager, rc);
                globus_gram_job_manager_log(
                        manager,
                        GLOBUS_GRAM_JOB_MANAGER_LOG_DEBUG,
                        "event=gram.new_request.info "
                        "level=DEBUG "
                        "gramid=%s "
                        "msg=\"%s\" "
                        "response=%d "
                        "\n",
                        old_job_contact ? old_job_contact : "",
                        globus_gram_protocol_error_string(rc),
                        rc);
            }
            rc = globus_gram_job_manager_reply(
                    NULL,
                    manager,
                    rc,
                    (old_job_contact == NULL && old_job_request)
                        ? old_job_request->job_contact
                        : old_job_contact,
                    response_fd,
                    context,
                    gt3_failure_message);

            if (gt3_failure_message)
            {
                free(gt3_failure_message);
            }

            done = GLOBUS_TRUE;
            rc = globus_gram_job_manager_gsi_update_credential(
                    manager,
                    NULL,
                    cred);
            cred = GSS_C_NO_CREDENTIAL;

            if (old_job_request)
            {
                /* This occurs when a client tries to restart a job that
                 * we found during the load_all when this process started, or
                 * one which had a two-phase end time out.
                 *
                 * We'll return information to the client about the job and
                 * make sure the job manager knows about the client
                 * contact/mask.
                 *
                 * If it is in a STOP state or two-phase end state, then we
                 * need to fake the restart by setting the state to just after
                 * two-phase commit and let the restart logic in the state
                 * machine pick it up from there.
                 * 
                 * Additionally, in the STOP state, we need to register the
                 * state machine.
                 */
                GlobusGramJobManagerRequestLock(old_job_request);
                globus_gram_job_manager_contact_add(
                        old_job_request,
                        contact,
                        job_state_mask);
                
                if (old_job_request->jobmanager_state ==
                        GLOBUS_GRAM_JOB_MANAGER_STATE_STOP)
                {
                    if (old_job_request->jm_restart)
                    {
                        free(old_job_request->jm_restart);
                    }
                    old_job_request->jm_restart = strdup(
                            old_job_request->job_contact);

                    /* In GLOBUS_GRAM_JOB_MANAGER_STATE_START,
                     * the state machine jumps to the current restart state
                     * based on the value in the state file after receiving
                     * a two-phase commit signal
                     */
                    old_job_request->jobmanager_state =
                        GLOBUS_GRAM_JOB_MANAGER_STATE_START;

                    old_job_request->unsent_status_change = GLOBUS_TRUE;

                    /* If the job is in another state, we'll assume that it's
                     * already being handled by the state machine
                     */
                    globus_gram_job_manager_state_machine_register(
                            old_job_request->manager,
                            old_job_request,
                            NULL);
                }
                else if (old_job_request->jobmanager_state ==
                                GLOBUS_GRAM_JOB_MANAGER_STATE_TWO_PHASE_END ||
                         old_job_request->jobmanager_state ==
                                GLOBUS_GRAM_JOB_MANAGER_STATE_FAILED_TWO_PHASE)
                {
                    if (old_job_request->jm_restart)
                    {
                        free(old_job_request->jm_restart);
                    }
                    old_job_request->jm_restart = strdup(old_job_request->job_contact);

                    /* In GLOBUS_GRAM_JOB_MANAGER_STATE_START,
                     * the state machine jumps to the current restart state
                     * based on the value in the state file.
                     */
                    old_job_request->jobmanager_state =
                        GLOBUS_GRAM_JOB_MANAGER_STATE_START;
                }
                GlobusGramJobManagerRequestUnlock(old_job_request);

                globus_gram_job_manager_remove_reference(
                        old_job_request->manager,
                        old_job_request->job_contact_path,
                        "jm_restart");
            }
            goto request_load_failed;
        }

        if (!version_only)
        {
            major_status = gss_inquire_cred(
                    &minor_status,
                    cred,
                    &name,
                    NULL,
                    NULL,
                    NULL);
            /* Don't care too much if this fails, as the user_dn will be
             * set to NULL in that case
             */
            if (major_status == GSS_S_COMPLETE)
            {
                major_status = gss_display_name(
                        &minor_status,
                        name,
                        &output_name,
                        NULL);
                if (request->job_stats.user_dn == NULL)
                {
                    request->job_stats.user_dn = strdup(output_name.value);
                }
                gss_release_name(&minor_status, &name);
                gss_release_buffer(&minor_status, &output_name);
            }
        }

        rc = globus_gram_job_manager_gsi_update_credential(
                manager,
                NULL,
                cred);
        cred = GSS_C_NO_CREDENTIAL;

        /* How much do I care about this error? */
        if (rc != GLOBUS_SUCCESS || version_only)
        {
            globus_gram_job_manager_reply(
                    request,
                    manager,
                    rc,
                    NULL,
                    response_fd,
                    context,
                    NULL);

            done = GLOBUS_TRUE;
            goto update_cred_failed;
        }

        if (!version_only)
        {
            globus_gram_job_manager_request_log(
                    request,
                    GLOBUS_GRAM_JOB_MANAGER_LOG_DEBUG,
                    "event=gram.job.start "
                    "level=DEBUG "
                    "gramid=%s "
                    "peer=%s "
                    "\n",
                    request->job_contact_path,
                    peername ? peername : "\"\"");
            
            /* Start state machine and send response */
            rc = globus_gram_job_manager_request_start(
                    manager,
                    request,
                    response_fd,
                    contact,
                    job_state_mask);
            if (rc != GLOBUS_SUCCESS)
            {
                globus_gram_job_manager_request_log(
                        request,
                        GLOBUS_GRAM_JOB_MANAGER_LOG_DEBUG,
                        "event=gram.job.end "
                        "level=DEBUG "
                        "gramid=%s "
                        "status=%d "
                        "msg=\"%s\" "
                        "reason=\"%s\" "
                        "\n",
                        request->job_contact_path,
                        -rc,
                        "Request start failed",
                        globus_gram_protocol_error_string(rc));
                globus_gram_job_manager_request_free(request);
                request = NULL;
            }
        }

update_cred_failed:
request_load_failed:
        if (contact)
        {
            free(contact);
        }

        if (old_job_contact != NULL)
        {
            free(old_job_contact);
        }
        if (cred != GSS_C_NO_CREDENTIAL)
        {
            gss_release_cred(
                    &minor_status,
                    &cred);
            cred = GSS_C_NO_CREDENTIAL;
        }
        if (peername != NULL)
        {
            free(peername);
        }
        if (peer_cred_handle != NULL)
        {
            globus_gsi_cred_handle_destroy(peer_cred_handle);
        }
ackfailed:
failed_import_cred:
        if (http_body_fd != -1)
        {
            close(http_body_fd);
            if (response_fd == http_body_fd)
            {
                response_fd = -1;
            }
            http_body_fd = -1;
        }
        if (context_fd != -1)
        {
            close(context_fd);
            context_fd = -1;
        }
        if (response_fd != -1)
        {
            close(response_fd);
            response_fd = -1;
        }
failed_read:
    free(message_buffer);
failed_malloc:
failed_receive:
        ;
    }
    if (cmsgbuf != NULL)
    {
        free(cmsgbuf);
    }
    if (http_body_fd != -1)
    {
        close(http_body_fd);
        if (response_fd == http_body_fd)
        {
            response_fd = -1;
        }
        http_body_fd = -1;
    }
    if (context_fd != -1)
    {
        close(context_fd);
        context_fd = -1;
    }
    if (response_fd != -1)
    {
        close(response_fd);
        response_fd = -1;
    }
    if (newsock != -1)
    {
        close(newsock);
        newsock = -1;
    }

    result = globus_xio_register_read(
            handle,
            buffer,
            0,
            0,
            NULL,
            globus_l_gram_startup_socket_callback,
            manager);
}
/* globus_l_gram_startup_socket_callback() */

static
globus_result_t
globus_l_gram_create_handle(
    int                                 sock,
    globus_xio_handle_t *               handle)
{
    globus_xio_attr_t                   attr;
    globus_result_t                     result;


    result = globus_xio_attr_init(&attr);
    if (result != GLOBUS_SUCCESS)
    {
        goto attr_init_failed;
    }
    result = globus_xio_attr_cntl(
            attr,
            globus_i_gram_job_manager_file_driver,
            GLOBUS_XIO_FILE_SET_HANDLE,
            sock);
    if (result != GLOBUS_SUCCESS)
    {
        goto attr_cntl_failed;
    }

    result = globus_xio_handle_create(
            handle,
            globus_i_gram_job_manager_file_stack);
    if (result != GLOBUS_SUCCESS)
    {
        goto handle_create_failed;
    }

    result = globus_xio_open(
            *handle,
            NULL,
            attr);

    if (result != GLOBUS_SUCCESS)
    {
        globus_xio_close(*handle, NULL);
        *handle = NULL;
    }

handle_create_failed:
attr_cntl_failed:
    globus_xio_attr_destroy(attr);
attr_init_failed:
    return result;
}
/* globus_l_gram_create_handle() */

static
int
globus_l_create_starter_connect_socket(
    globus_gram_job_manager_t *         manager,
    int *                               sockptr)
{
    int                                 rc;
    struct sockaddr_un                  addr;
    int                                 sock = -1;
    char *                              msg = "";
    int                                 sndbuf;
    int                                 save_errno = 0;

    /* create socket */
    memset(&addr, 0, sizeof(struct sockaddr_un));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, manager->socket_path, sizeof(addr.sun_path)-1);
    sock = socket(AF_UNIX, SOCK_STREAM, 0);
    if (sock < 0)
    {
        rc = GLOBUS_GRAM_PROTOCOL_ERROR_NO_RESOURCES;
        save_errno = errno;
        msg = "Error creating datagram socket";

        goto socket_failed;
    }
    sndbuf = GLOBUS_GRAM_PROTOCOL_MAX_MSG_SIZE;
    rc = setsockopt(sock, SOL_SOCKET, SO_SNDBUF, &sndbuf, sizeof(sndbuf));
    if (rc < 0 && errno != ENOPROTOOPT)
    {
        rc = GLOBUS_GRAM_PROTOCOL_ERROR_NO_RESOURCES;
        save_errno = errno;
        msg = "Error setting datagram socket buffer";

        goto setsockopt_failed;
    }
    rc = connect(sock, (struct sockaddr *) &addr, sizeof(addr));
    if (rc < 0)
    {
        rc = GLOBUS_GRAM_PROTOCOL_ERROR_NO_RESOURCES;
        save_errno = errno;
        msg = "Error making datagram connection to Job Manager";

setsockopt_failed:
        close(sock);
        sock = -1;
socket_failed:
        globus_gram_job_manager_log(
                manager,
                GLOBUS_GRAM_JOB_MANAGER_LOG_WARN,
                "event=gram.send_job.end "
                "level=WARN "
                "status=%d "
                "errno=%d "
                "msg=\"%s\" "
                "reason=\"%s\" "
                "path=\"%s\" "
                "\n",
                -rc,
                save_errno,
                msg,
                strerror(save_errno),
                addr.sun_path);
    }
    *sockptr = sock;
    return rc;
}
/* globus_l_create_starter_connect_socket() */

static
int
globus_l_encode_gatekeeper_env(
    struct iovec                       *iov)
{
    int envno;
    char *p;
    char cert_chain_name[] = "SSL_CLIENT_CERT_CHAINXX"; 
    int rc;

    iov->iov_len = 0;
    for (envno = 0; globus_l_gatekeeper_env[envno] != NULL; envno++)
    {
        const char * tmpstr = getenv(globus_l_gatekeeper_env[envno]);

        if (tmpstr != NULL)
        {
            iov->iov_len += strlen(globus_l_gatekeeper_env[envno]) + strlen(tmpstr) + 2;
        }
    }

    for (envno = 0; envno < 100; envno++)
    {
        const char * tmpstr;
        sprintf(cert_chain_name, "SSL_CLIENT_CERT_CHAIN%d", envno);

        tmpstr = getenv(cert_chain_name);
        if (tmpstr != NULL)
        {
            iov->iov_len += strlen(cert_chain_name) + strlen(tmpstr) + 2;
        }
    }

    iov->iov_base = malloc(iov->iov_len);
    p = iov->iov_base;

    for (envno = 0; globus_l_gatekeeper_env[envno] != NULL; envno++)
    {
        const char * tmpstr = getenv(globus_l_gatekeeper_env[envno]);

        if (tmpstr != NULL)
        {
            rc = sprintf(p, "%s=%s", globus_l_gatekeeper_env[envno], tmpstr);
            p += rc;
            *(p++) = 0;
        }
    }
    for (envno = 0; envno < 100; envno++)
    {
        const char * tmpstr;
        sprintf(cert_chain_name, "SSL_CLIENT_CERT_CHAIN%d", envno);

        tmpstr = getenv(cert_chain_name);
        if (tmpstr != NULL)
        {
            rc = sprintf(p, "%s=%s", cert_chain_name, tmpstr);
            p += rc;
            *(p++) = 0;
        }
    }
    return GLOBUS_SUCCESS;
}

static
int
globus_l_decode_gatekeeper_env(
    const char                         *p,
    size_t                              len,
    char                              **remote_addr,
    size_t                             *content_length,
    globus_gsi_cred_handle_t           *cred_handle)
{
    const char * endp = p + len;
    const char * cert_pem;
    X509 * cert;
    BIO * b = NULL;
    STACK_OF(X509) * chain = NULL;

    chain = sk_X509_new_null();

    globus_gsi_cred_handle_init(cred_handle, NULL);

    b = BIO_new(BIO_s_mem());

    while (p < endp)
    {
        if (strncmp(p, "REMOTE_ADDR=", 12) == 0)
        {
            *remote_addr = globus_common_create_string("%s", p+13);
        }
        else if (strncmp(p, "CONTENT_LENGTH", 14) == 0)
        {
            sscanf(p+15, "%zu", content_length);
        }
        /* SSL_CLIENT_CERT_CHAIN must be before SSL_CLIENT_CERT */
        else if (strncmp(p, "SSL_CLIENT_CERT_CHAIN", 21) == 0)
        {
            int chain_index;

            sscanf(p + 21, "%d", &chain_index);

            cert_pem = strchr(p, '=') + 1;
            BIO_write(b, cert_pem, strlen(cert_pem));
            cert = PEM_read_bio_X509(b, NULL, 0, NULL);

            sk_X509_insert(chain, cert, chain_index);
        }
        else if (strncmp(p, "SSL_CLIENT_CERT", 15) == 0)
        {
            cert_pem = p + 16;
            BIO_write(b, cert_pem, strlen(cert_pem));
            cert = PEM_read_bio_X509(b, NULL, 0, NULL);

            globus_gsi_cred_set_cert(*cred_handle, cert);
            X509_free(cert);
            cert = NULL;
        }
        p += strlen(p) + 1;
    }
    globus_gsi_cred_set_cert_chain(*cred_handle, chain);
    sk_X509_pop_free(chain, X509_free);
    BIO_free(b);

    return GLOBUS_SUCCESS;
}
/* globus_l_decode_gatekeeper_env() */

/**
 * Send a unix domain message containing an integer and an array of
 * file descriptors. The integer is encoded as as a 4 byte binary
 * value in network byte order. The file descriptors are sent as
 * ancillary data in the message.
 *
 * @param fd
 *     File descriptor to send the message on
 * @param msg_length
 *     Integer to send as the data payload of the message in encoded form.
 * @param fds
 *     Array of file descriptors to send via message control rights header.
 * @param fd_count
 *     Number of elements in the @a fds array.
 *
 * @return This function returns GLOBUS_SUCCESS when the message is
 * sent successfully, or a GRAM protocol error if an error occurs.
 */
static
int
globus_l_blocking_send_length_and_fds(
    int                                 fd,
    int                                 msg_length,
    int *                               fds,
    int                                 fd_count)
{
    struct msghdr                       message;
    struct cmsghdr *                    cmsg;
    void                               *cmsgbuf;
    unsigned char                       msg_length_buf[4];
    struct iovec                        iov[1];
    int                                *fdptr;
    int                                 i, rc;

    cmsgbuf = calloc(1, CMSG_SPACE(fd_count * sizeof(int)));

    msg_length_buf[0] = (msg_length >> 24) & 0xff;
    msg_length_buf[1] = (msg_length >> 16) & 0xff;
    msg_length_buf[2] = (msg_length >> 8)  & 0xff;
    msg_length_buf[3] = (msg_length)       & 0xff;

    /* Credential we send along with the descriptors */
    iov[0].iov_base = msg_length_buf;
    iov[0].iov_len = 4;

    /* Message metadata */
    message.msg_name = NULL;
    message.msg_namelen = 0;
    message.msg_iov = iov;
    message.msg_iovlen = 1;
    message.msg_flags = 0;
    message.msg_control = cmsgbuf;
    message.msg_controllen = CMSG_SPACE(fd_count*sizeof(int));

    cmsg = CMSG_FIRSTHDR(&message);

    assert(cmsg != NULL);

    cmsg->cmsg_level = SOL_SOCKET;
    cmsg->cmsg_type = SCM_RIGHTS;
    cmsg->cmsg_len = CMSG_LEN(fd_count * sizeof(int));
    fdptr = (int *)CMSG_DATA(cmsg);

    for (i = 0; i < fd_count; i++)
    {
        fdptr[i] = fds[i];
    }

    /* Send message length and fds */
    while ((rc = sendmsg(fd, &message, 0)) < message.msg_iov[0].iov_len)
    {
        if (rc < 0 && (errno == EAGAIN || errno == EINTR))
        {
            continue;
        }
        else if (rc < 0)
        {
            rc = GLOBUS_GRAM_PROTOCOL_ERROR_PROTOCOL_FAILED;
            goto sendmsg_failed;
        }
        message.msg_iov[0].iov_base =
                ((char *)message.msg_iov[0].iov_base) + rc;
        message.msg_iov[0].iov_len -= rc;

        message.msg_control = NULL;
        message.msg_controllen = 0;
    }
    rc = GLOBUS_SUCCESS;

sendmsg_failed:
    free(cmsgbuf);

    return rc;
}
/* globus_l_blocking_send_length_and_fds() */

/**
 * @brief Blocking writev
 *
 * Write an array of data to a file descriptor, looping and blocking until the 
 * entire array is written or an error occurs. Note: this function
 * destructively modifies iov, so if there are pointers to data on the heap,
 * they must be retained elsewhere for proper memory management.
 *
 * @param fd
 *     The file descriptor to write to.
 * @param iov
 *     The array of struct iovec containing the data to write
 * @param iovcnt
 *     Number of elements in the @a iov array.
 * 
 * @return This function 
 */
static
int
globus_l_blocking_writev(
    int                                 fd,
    struct iovec *                      iov,
    int                                 iovcnt)
{
    int                                 i, rc = 0, amt_written = 0, total;

    for (i = 0, total = 0; i < iovcnt; i++)
    {
        total += iov[i].iov_len;
    }

    do
    {
        rc = writev(fd, iov, iovcnt);

        if (rc < 0)
        {
            if (errno == EINTR || errno == EAGAIN)
            {
                globus_libc_usleep(10000);
                rc = 0;
            }
            else
            {
                break;
            }
        }
        else
        {
            amt_written += rc;

            for (i = 0; rc > 0 && i < iovcnt; i++)
            {
                if (rc > iov[i].iov_len)
                {
                    rc -= iov[i].iov_len;
                    iov[i].iov_base = ((char *) iov[i].iov_base)
                            + iov[i].iov_len;
                    iov[i].iov_len = 0;
                }
                else
                {
                    iov[i].iov_len -= rc;
                    iov[i].iov_base = ((char *) iov[i].iov_base) + rc;
                    rc = 0;
                    break;
                }
            }

        }
    }
    while (amt_written < total);

    return amt_written;
}
/* globus_l_blocking_writev() */

/**
 * Read a known-size message from a file descriptor, handling
 * partial reads, EINTR, and EAGAIN
 * 
 * @param fd
 *     File descriptor to read from
 * @param buf
 *     Buffer (of at least @a len bytes) to read into.
 * @param len
 *     Total expected amount of data to read into @a buffer.
 *
 * @return
 *     This function returns the total number of bytes read into @a buffer. 
 *     This may be less than @a len if an unexpected error occurs while reading
 *     the file descriptor.
 */
static
int
globus_l_blocking_read(
    int                                 fd,
    void                               *buf,
    int                                 len)
{
    int                                 amt_read = 0, rc;

    do
    {
        rc = read(fd, ((char *)buf)+amt_read, len-amt_read);

        if (rc < 0 && (errno == EINTR || errno == EAGAIN))
        {
            globus_libc_usleep(10000);
            rc = 0;
        }
        else if (rc < 0)
        {
            break;
        }
        amt_read += rc;
    }
    while (amt_read < len && rc >= 0);

    return amt_read;
}
/* globus_l_blocking_read() */

/**
 * Remove a proxy named by a buffer in GSS_IMPEXP_MECH_SPECIFIC form
 * The token may not be NULL-terminated, so we will NULL-terminate explicitly
 * before trying to remove it.
 *
 * @param token
 *     A GSSAPI cred token which is in GSS_IMPEXP_MECH_SPECIFIC form
 *     X509_USER_PROXY=path-to-proxy
 *
 * @return void
 */
static
void
globus_l_remove_proxy(
    gss_buffer_t                        token)
{
    char * p;
    char * q;
    size_t len;

    q = memchr(token->value, '=', token->length);
    if (q == NULL)
    {
        return;
    }

    len = token->length - (q - (char *) token->value);

    p = malloc(len);
    if (p == NULL)
    {
        return;
    }
    memcpy(p, q+1, len-1);
    p[len-1] = 0;

    (void) remove(p);
    free(p);
}
/* globus_l_remove_proxy() */
