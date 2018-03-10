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

#include "globus_xio_driver.h"
#include "globus_xio_udp_driver.h"
#include "version.h"

#ifndef WIN32
#include <netinet/udp.h>
#endif

static
int
globus_l_xio_udp_activate(void);

static
int
globus_l_xio_udp_deactivate(void);

GlobusXIODefineModule(udp) =
{
    "globus_xio_udp",
    globus_l_xio_udp_activate,
    globus_l_xio_udp_deactivate,
    GLOBUS_NULL,
    GLOBUS_NULL,
    &local_version
};

#define GlobusXIOUdpErrorNoAddrs()                                          \
    globus_error_put(                                                       \
        globus_error_construct_error(                                       \
            GlobusXIOMyModule(udp),                                         \
            GLOBUS_NULL,                                                    \
            GLOBUS_XIO_UDP_ERROR_NO_ADDRS,                                  \
            __FILE__,                                                       \
            _xio_name,                                                      \
            __LINE__,                                                       \
            "No addrs for INET family"))

#define GlobusXIOUdpErrorShortWrite()                                       \
    globus_error_put(                                                       \
        globus_error_construct_error(                                       \
            GlobusXIOMyModule(udp),                                         \
            GLOBUS_NULL,                                                    \
            GLOBUS_XIO_UDP_ERROR_SHORT_WRITE,                               \
            __FILE__,                                                       \
            _xio_name,                                                      \
            __LINE__,                                                       \
            "Unable to write full request"))

/*
 *  attribute structure
 */
typedef struct
{
    /* handle attrs */
    globus_xio_system_socket_t          fd;
    char *                              listener_serv;
    int                                 listener_port;
    int                                 listener_min_port;
    int                                 listener_max_port;
    char *                              bind_address;
    globus_bool_t                       restrict_port;
    globus_bool_t                       reuseaddr;
    globus_bool_t                       no_ipv6;
    int                                 sndbuf;
    int                                 rcvbuf;
    globus_bool_t                       join_multicast;
    globus_sockaddr_t                   multicast_addr;
    
    /* dd attrs */
    globus_bool_t                       use_addr;
    globus_sockaddr_t                   addr;
} globus_l_attr_t;

/* default attr */
static globus_l_attr_t                  globus_l_xio_udp_attr_default =
{
    GLOBUS_XIO_UDP_INVALID_HANDLE,      /* fd */ 
    
    GLOBUS_NULL,                        /* listener_serv */
    0,                                  /* listener_port */
    0,                                  /* listener_min_port */
    0,                                  /* listener_max_port */
    GLOBUS_NULL,                        /* bind_address */
    GLOBUS_TRUE,                        /* restrict_port */
    GLOBUS_FALSE,                       /* reuseaddr */
    GLOBUS_FALSE,                       /* no_ipv6 */
    0,                                  /* sndbuf (system default) */     
    0,                                  /* rcvbuf (system default) */
    GLOBUS_FALSE,                       /* join_multicast */
    {0},                                /* multicast_addr */
    
    GLOBUS_FALSE,                       /* use_addr */
    {0}                                 /* addr */
};

static globus_xio_string_cntl_table_t   udp_l_string_opts_table[] =
{
    {"port", GLOBUS_XIO_UDP_SET_SERVICE,
        globus_xio_string_cntl_string},
    {"listen_range", GLOBUS_XIO_UDP_SET_LISTEN_RANGE,
        globus_xio_string_cntl_int_int},
    {"iface", GLOBUS_XIO_UDP_SET_INTERFACE,
        globus_xio_string_cntl_string},
    {"reuse", GLOBUS_XIO_UDP_SET_REUSEADDR,
        globus_xio_string_cntl_bool},
    {"noipv6", GLOBUS_XIO_UDP_SET_NO_IPV6,
        globus_xio_string_cntl_bool},
    {"sndbuf", GLOBUS_XIO_UDP_SET_SNDBUF,
        globus_xio_string_cntl_formated_int},
    {"rcvbuf", GLOBUS_XIO_UDP_SET_RCVBUF,
        globus_xio_string_cntl_formated_int},
    {NULL, 0, NULL }
};

/*
 *  handle structure
 */
typedef struct
{
    globus_xio_system_socket_handle_t   system;
    globus_xio_system_socket_t          fd;
    int                                 socket_sa_family;
    globus_bool_t                       connected;
    globus_bool_t                       converted;
    globus_bool_t                       no_ipv6;
} globus_l_handle_t;

static
globus_bool_t
globus_l_xio_udp_get_env_pair(
    const char *                        env_name,
    int *                               min,
    int *                               max)
{
    char *                              min_max;
    GlobusXIOName(globus_l_xio_udp_get_env_pair);

    min_max = globus_module_getenv(env_name);

    if(min_max && sscanf(min_max, " %d , %d", min, max) == 2)
    {
        return GLOBUS_TRUE;
    }

    return GLOBUS_FALSE;
}

/*
 *  initialize a driver attribute
 */
static
globus_result_t
globus_l_xio_udp_attr_init(
    void **                             out_attr)
{
    globus_l_attr_t *                   attr;
    globus_result_t                     result;
    GlobusXIOName(globus_l_xio_udp_attr_init);
    
    /*
     *  create a udp attr structure and intialize its values
     */
    attr = (globus_l_attr_t *) globus_malloc(sizeof(globus_l_attr_t));
    if(!attr)
    {
        result = GlobusXIOErrorMemory("attr");
        goto error_attr;
    }
    
    memcpy(attr, &globus_l_xio_udp_attr_default, sizeof(globus_l_attr_t));
    *out_attr = attr;

    return GLOBUS_SUCCESS;

error_attr:
    return result;
}

static
globus_result_t
globus_l_xio_udp_get_addrinfo(
    const char *                        host,
    const char *                        port,
    globus_addrinfo_t **                addrinfo,
    globus_bool_t                       no_ipv6)
{
    globus_result_t                     result;
    globus_addrinfo_t                   addrinfo_hints;
    GlobusXIOName(globus_l_xio_udp_get_addrinfo);
    
    /* setup hints for types of connectable sockets we want */
    memset(&addrinfo_hints, 0, sizeof(globus_addrinfo_t));
    addrinfo_hints.ai_flags = 0;
    addrinfo_hints.ai_family = no_ipv6 ? PF_INET : PF_UNSPEC;
    addrinfo_hints.ai_socktype = SOCK_DGRAM;
    addrinfo_hints.ai_protocol = 0;
    
    result = globus_libc_getaddrinfo(
        host, port, &addrinfo_hints, addrinfo);
    if(result != GLOBUS_SUCCESS)
    {
        result = GlobusXIOErrorWrapFailed(
            "globus_libc_getaddrinfo", result);
        goto error_getaddrinfo;
    }
    
    return GLOBUS_SUCCESS;

error_getaddrinfo:
    return result;
}

/*
 *  modify the attribute structure
 */
static
globus_result_t
globus_l_xio_udp_attr_cntl(
    void *                              driver_attr,
    int                                 cmd,
    va_list                             ap)
{
    globus_l_attr_t *                   attr;
    globus_xio_system_socket_t *        out_fd;
    char **                             out_string;
    int *                               out_int;
    globus_bool_t *                     out_bool;
    char *                              in_string;
    globus_result_t                     result;
    int                                 flags;
    globus_addrinfo_t *                 addrinfo;
    globus_addrinfo_t *                 save_addrinfo;
    globus_xio_contact_t                contact_info;
    GlobusXIOName(globus_l_xio_udp_attr_cntl);

    attr = (globus_l_attr_t *) driver_attr;
    flags = 0;
    switch(cmd)
    {
      /**
       *  handle attrs
       */
      /* globus_xio_system_socket_t     fd */
      case GLOBUS_XIO_UDP_SET_HANDLE:
        attr->fd = va_arg(ap, globus_xio_system_socket_t);
        break;
      
      /* globus_xio_system_socket_t *   fd_out */
      case GLOBUS_XIO_UDP_GET_HANDLE:
        out_fd = va_arg(ap, globus_xio_system_socket_t *);
        *out_fd = attr->fd;
        break;
       
      /* char *                         service_name */
      case GLOBUS_XIO_UDP_SET_SERVICE:
        if(attr->listener_serv)
        {
            free(attr->listener_serv);
        }
        
        attr->listener_serv = va_arg(ap, char *);
        if(attr->listener_serv)
        {
            attr->listener_serv = globus_libc_strdup(attr->listener_serv);
            if(!attr->listener_serv)
            {
                result = GlobusXIOErrorMemory("listener_serv");
                goto error_memory;
            }
        }
        break;
      
      /* char **                        service_name_out */
      case GLOBUS_XIO_UDP_GET_SERVICE:
        out_string = va_arg(ap, char **);
        if(attr->listener_serv)
        {
            *out_string = globus_libc_strdup(attr->listener_serv);
            if(!*out_string)
            {
                result = GlobusXIOErrorMemory("listener_serv_out");
                goto error_memory;
            }
        }
        else
        {
            *out_string = GLOBUS_NULL;
        }
        break;
      
      /* int                            listener_port */
      case GLOBUS_XIO_UDP_SET_PORT:
        attr->listener_port = va_arg(ap, int);
        break;
      
      /* int *                          listener_port_out */
      case GLOBUS_XIO_UDP_GET_PORT:
        out_int = va_arg(ap, int *);
        *out_int = attr->listener_port;
        break;
      
      /* int                            listener_min_port */
      /* int                            listener_max_port */
      case GLOBUS_XIO_UDP_SET_LISTEN_RANGE:
        attr->listener_min_port = va_arg(ap, int);
        attr->listener_max_port = va_arg(ap, int);
        break;
      
      /* int *                          listener_min_port_out */
      /* int *                          listener_max_port_out */
      case GLOBUS_XIO_UDP_GET_LISTEN_RANGE:
        out_int = va_arg(ap, int *);
        *out_int = attr->listener_min_port;
        out_int = va_arg(ap, int *);
        *out_int = attr->listener_max_port;
        break;
        
      /* char *                         interface */
      case GLOBUS_XIO_UDP_SET_INTERFACE:
        if(attr->bind_address)
        {
            free(attr->bind_address);
        }
        
        attr->bind_address = va_arg(ap, char *);
        if(attr->bind_address)
        {
            attr->bind_address = globus_libc_strdup(attr->bind_address);
            if(!attr->bind_address)
            {
                result = GlobusXIOErrorMemory("bind_address");
                goto error_memory;
            }
        }
        break;
      
      /* char **                        interface_out */
      case GLOBUS_XIO_UDP_GET_INTERFACE:
        out_string = va_arg(ap, char **);
        if(attr->bind_address)
        {
            *out_string = globus_libc_strdup(attr->bind_address);
            if(!*out_string)
            {
                result = GlobusXIOErrorMemory("bind_address_out");
                goto error_memory;
            }
        }
        else
        {
            *out_string = GLOBUS_NULL;
        }
        break;
      
      /* globus_bool_t                  restrict_port */
      case GLOBUS_XIO_UDP_SET_RESTRICT_PORT:
        attr->restrict_port = va_arg(ap, globus_bool_t);
        break;
      
      /* globus_bool_t *                restrict_port_out */
      case GLOBUS_XIO_UDP_GET_RESTRICT_PORT:
        out_bool = va_arg(ap, globus_bool_t *);
        *out_bool = attr->restrict_port;
        break;
      
      /* globus_bool_t                  reuseaddr */
      case GLOBUS_XIO_UDP_SET_REUSEADDR:
        attr->reuseaddr = va_arg(ap, globus_bool_t);
        break;
        
      /* globus_bool_t *                reuseaddr_out */
      case GLOBUS_XIO_UDP_GET_REUSEADDR:
        out_bool = va_arg(ap, globus_bool_t *);
        *out_bool = attr->reuseaddr;
        break;
      
      /* globus_bool_t                  no_ipv6 */
      case GLOBUS_XIO_UDP_SET_NO_IPV6:
        attr->no_ipv6 = va_arg(ap, globus_bool_t);
        break;
        
      /* globus_bool_t *                no_ipv6_out */
      case GLOBUS_XIO_UDP_GET_NO_IPV6:
        out_bool = va_arg(ap, globus_bool_t *);
        *out_bool = attr->no_ipv6;
        break;
    
      /* int                            sndbuf */
      case GLOBUS_XIO_UDP_SET_SNDBUF:
        attr->sndbuf = va_arg(ap, int);
        break;
        
      /* int *                          sndbuf_out */
      case GLOBUS_XIO_UDP_GET_SNDBUF:
        out_int = va_arg(ap, int *);
        *out_int = attr->sndbuf;
        break;
        
      /* int                            rcvbuf */
      case GLOBUS_XIO_UDP_SET_RCVBUF:
        attr->rcvbuf = va_arg(ap, int);
        break;
        
      /* int *                          rcvbuf_out */
      case GLOBUS_XIO_UDP_GET_RCVBUF:
        out_int = va_arg(ap, int *);
        *out_int = attr->rcvbuf;
        break;
        
      /**
       * dd attrs
       */
      /* char **                        contact_string_out */
      case GLOBUS_XIO_UDP_GET_NUMERIC_CONTACT:
        flags |= GLOBUS_LIBC_ADDR_NUMERIC;
        /* fall through */
      
      /* char **                        contact_string_out */
      case GLOBUS_XIO_UDP_GET_CONTACT:
        if(attr->use_addr)
        {
            out_string = va_arg(ap, char **);
            result = globus_libc_addr_to_contact_string(
                &attr->addr,
                flags,
                out_string);
            if(result != GLOBUS_SUCCESS)
            {
                result = GlobusXIOErrorWrapFailed(
                    "globus_libc_addr_to_contact_string", result);
                goto error_contact;
            }
        }
        else
        {
            result = GlobusXIOUdpErrorNoAddrs();
            goto error_contact;
        }
        break;
      
      /* char *                         contact_string */
      case GLOBUS_XIO_UDP_SET_CONTACT:
      case GLOBUS_XIO_UDP_SET_MULTICAST:
        in_string = va_arg(ap, char *);
        
        result = globus_xio_contact_parse(&contact_info, in_string);
        if(result != GLOBUS_SUCCESS)
        {
            result = GlobusXIOErrorWrapFailed(
                "globus_xio_contact_parse", result);
            goto error_getaddrinfo;
        }
        
        save_addrinfo = GLOBUS_NULL;
        if(contact_info.host && contact_info.port)
        {
            result = globus_l_xio_udp_get_addrinfo(
                contact_info.host,
                contact_info.port,
                &save_addrinfo,
                attr->no_ipv6);
            globus_xio_contact_destroy(&contact_info);
            if(result != GLOBUS_SUCCESS)
            {
                result = GlobusXIOErrorWrapFailed(
                    "globus_l_xio_udp_get_addrinfo", result);
                goto error_getaddrinfo;
            }
        }
        
        if(save_addrinfo)
        {
            for(addrinfo = save_addrinfo;
                addrinfo && !GlobusLibcProtocolFamilyIsIP(addrinfo->ai_family);
                addrinfo = addrinfo->ai_next)
            {
            }
        
            if(addrinfo)
            {
                if(cmd == GLOBUS_XIO_UDP_SET_CONTACT)
                {
                    result = globus_libc_addr_convert_family(
                       (globus_sockaddr_t *) addrinfo->ai_addr,
                       &attr->addr,
                       attr->no_ipv6 ? AF_INET : AF_INET6);
                    if(result != GLOBUS_SUCCESS)
                    {
                       result = GlobusXIOErrorWrapFailed(
                           "globus_libc_addr_convert_family", result);
                       goto error_no_addrinfo;
                    }
                    attr->use_addr = GLOBUS_TRUE;
                }
                else
                {
                    GlobusLibcSockaddrCopy(attr->multicast_addr,
                        *addrinfo->ai_addr, addrinfo->ai_addrlen);
                    attr->join_multicast = GLOBUS_TRUE;
                }
            }
            else
            {
                result = GlobusXIOUdpErrorNoAddrs();
                goto error_no_addrinfo;
            }
        
            globus_libc_freeaddrinfo(save_addrinfo);
        }
        else
        {
            globus_xio_contact_destroy(&contact_info);
            
            if(cmd == GLOBUS_XIO_UDP_SET_CONTACT)
            {
                attr->use_addr = GLOBUS_FALSE;
            }
            else
            {
                attr->join_multicast = GLOBUS_FALSE;
            }
        }
        
        break;

      case GLOBUS_XIO_GET_STRING_OPTIONS:
        out_string = va_arg(ap, char **);
        size_t string_opts_len = 1;
        if (attr->listener_serv)
        {
            string_opts_len += snprintf(
                NULL, 0, "port=%s;", attr->listener_serv);
        }
        if (attr->listener_min_port || attr->listener_max_port)
        {
            string_opts_len += snprintf(
                NULL, 0,
                "listen_range=%d %d;",
                attr->listener_min_port,
                attr->listener_max_port);
        }
       
        if (attr->bind_address)
        {
            string_opts_len += snprintf(
                NULL, 0,
                "iface=%s;",
                attr->bind_address);
        }

        string_opts_len += snprintf(
                NULL, 0,
                "reuse=%s;",
                attr->reuseaddr ? "true" : "false");
        string_opts_len += snprintf(
                NULL, 0,
                "noipv6=%s;",
                attr->no_ipv6 ? "true" : "false");
        if (attr->sndbuf)
        {
            string_opts_len += snprintf(
                    NULL, 0,
                    "sndbuf=%d;",
                    attr->sndbuf);
        }
        if (attr->rcvbuf)
        {
            string_opts_len += snprintf(
                    NULL, 0,
                    "rcvbuf=%d;",
                    attr->rcvbuf);
        }
        *out_string = malloc(string_opts_len);

        string_opts_len = 0;
        if (attr->listener_serv)
        {
            string_opts_len += sprintf(
                *out_string + string_opts_len,
                "port=%s;", attr->listener_serv);
        }
        if (attr->listener_min_port || attr->listener_max_port)
        {
            string_opts_len += sprintf(
                *out_string + string_opts_len,
                "listen_range=%d %d;",
                attr->listener_min_port,
                attr->listener_max_port);
        }
       
        if (attr->bind_address)
        {
            string_opts_len += sprintf(
                *out_string + string_opts_len,
                "iface=%s;",
                attr->bind_address);
        }

        string_opts_len += sprintf(
                *out_string + string_opts_len,
                "reuse=%s;",
                attr->reuseaddr ? "true" : "false");
        string_opts_len += sprintf(
                *out_string + string_opts_len,
                "noipv6=%s;",
                attr->no_ipv6 ? "true" : "false");
        if (attr->sndbuf)
        {
            string_opts_len += sprintf(
                    *out_string + string_opts_len,
                    "sndbuf=%d;",
                    attr->sndbuf);
        }
        if (attr->rcvbuf)
        {
            string_opts_len += sprintf(
                    *out_string + string_opts_len,
                    "rcvbuf=%d;",
                    attr->rcvbuf);
        }
        *((*out_string) + string_opts_len - 1) = '\0';
        break;


      default:
        result = GlobusXIOErrorInvalidCommand(cmd);
        goto error_invalid;
        break;
    }

    return GLOBUS_SUCCESS;

error_no_addrinfo:
    globus_libc_freeaddrinfo(save_addrinfo);

error_getaddrinfo:
error_memory:
error_contact:
error_invalid:
    return result;
}

/*
 *  copy an attribute structure
 */
static
globus_result_t
globus_l_xio_udp_attr_copy(
    void **                             dst,
    void *                              src)
{
    globus_l_attr_t *                   attr;
    globus_result_t                     result;
    GlobusXIOName(globus_l_xio_udp_attr_copy);

    attr = (globus_l_attr_t *) globus_malloc(sizeof(globus_l_attr_t));
    if(!attr)
    {
        result = GlobusXIOErrorMemory("attr");
        goto error_attr;
    }
    
    memcpy(attr, src, sizeof(globus_l_attr_t));
    if(attr->bind_address)
    {
        attr->bind_address = globus_libc_strdup(attr->bind_address);
        if(!attr->bind_address)
        {
            result = GlobusXIOErrorMemory("bind_address");
            goto error_bind_address;
        }
    }
    if(attr->listener_serv)
    {
        attr->listener_serv = globus_libc_strdup(attr->listener_serv);
        if(!attr->listener_serv)
        {
            result = GlobusXIOErrorMemory("listener_serv");
            goto error_listener_serv;
        }
    }
    
    *dst = attr;

    return GLOBUS_SUCCESS;

error_listener_serv:
    if(attr->bind_address)
    {
        free(attr->bind_address);
    }
    
error_bind_address:
    free(attr);
    
error_attr:
    return result;
}

/*
 *  destroy an attr structure
 */
static
globus_result_t
globus_l_xio_udp_attr_destroy(
    void *                              driver_attr)
{
    globus_l_attr_t *                   attr;
    GlobusXIOName(globus_l_xio_udp_attr_destroy);
    
    attr = (globus_l_attr_t *) driver_attr;
    if(attr->bind_address)
    {
        free(attr->bind_address);
    }
    if(attr->listener_serv)
    {
        free(attr->listener_serv);
    }
    
    free(attr);

    return GLOBUS_SUCCESS;
}

static
globus_result_t
globus_l_xio_udp_apply_handle_attrs(
    const globus_l_attr_t *             attr,
    globus_xio_system_socket_t          fd,
    globus_bool_t                       converted)
{
    globus_result_t                     result;
    int                                 int_one = 1;
    GlobusXIOName(globus_l_xio_udp_apply_handle_attrs);
    
    if(attr->reuseaddr)
    {
        result = globus_xio_system_socket_setsockopt(
            fd, SOL_SOCKET, SO_REUSEADDR, &int_one, sizeof(int_one));
        if(result != GLOBUS_SUCCESS)
        {
            goto error_sockopt;
        }
    }
    
    if(attr->sndbuf)
    {
        result = globus_xio_system_socket_setsockopt(
            fd, SOL_SOCKET, SO_SNDBUF, &attr->sndbuf, sizeof(attr->sndbuf));
        if(result != GLOBUS_SUCCESS)
        {
            goto error_sockopt;
        }
    }
    
    if(attr->rcvbuf)
    {
        result = globus_xio_system_socket_setsockopt(
            fd, SOL_SOCKET, SO_RCVBUF, &attr->rcvbuf, sizeof(attr->rcvbuf));
        if(result != GLOBUS_SUCCESS)
        {
            goto error_sockopt;
        }
    }
    
    return GLOBUS_SUCCESS;

error_sockopt:
    return result;
}

static
globus_result_t
globus_l_xio_udp_bind(
    globus_xio_system_socket_t          fd,
    const struct sockaddr *             addr,
    int                                 addr_len,
    int                                 min_port,
    int                                 max_port)
{
    int                                 port;
    globus_bool_t                       done;
    globus_sockaddr_t                   myaddr;
    globus_result_t                     result;
    GlobusXIOName(globus_l_xio_udp_bind);
    
    GlobusLibcSockaddrGetPort(*addr, port);
    
    if(port == 0)
    {
        port = min_port;
    }
    else
    {
        max_port = port;
    }
    
    done = GLOBUS_FALSE;
    do
    {
        GlobusLibcSockaddrCopy(myaddr, *addr, addr_len);
        GlobusLibcSockaddrSetPort(myaddr, port);
        
        result = globus_xio_system_socket_bind(
            fd,
            (struct sockaddr *) &myaddr,
            GlobusLibcSockaddrLen(&myaddr));
        if(result != GLOBUS_SUCCESS)
        {
            if(++port > max_port)
            {
                goto error_bind;
            }
        }
        else
        {
            done = GLOBUS_TRUE;
        }
    } while(!done);

    return GLOBUS_SUCCESS;
    
error_bind:
    return result;
}

static
globus_result_t
globus_l_xio_udp_join_multicast(
    globus_l_handle_t *                 handle,
    const globus_l_attr_t *             attr)
{
    globus_result_t                     result;
    globus_xio_system_socket_t          fd;
    GlobusXIOName(globus_l_xio_udp_join_multicast);
    
    result = globus_xio_system_socket_create(
        &fd,
        GlobusLibcSockaddrGetFamily(attr->multicast_addr),
        SOCK_DGRAM,
        0);
    if(result != GLOBUS_SUCCESS)
    {
        goto error_socket;
    }
    result = globus_l_xio_udp_apply_handle_attrs(
        attr, fd, GLOBUS_FALSE);
    if(result != GLOBUS_SUCCESS)
    {
        result = GlobusXIOErrorWrapFailed(
            "globus_l_xio_udp_apply_handle_attrs", result);
        goto error_attrs;
    }
    
    result = globus_l_xio_udp_bind(
        fd,
        (struct sockaddr *) &attr->multicast_addr,
        GlobusLibcSockaddrLen(&attr->multicast_addr),
        0,
        0);
    if(result != GLOBUS_SUCCESS)
    {
        result = GlobusXIOErrorWrapFailed(
            "globus_l_xio_udp_bind", result);
        goto error_bind;
    }
    
#ifdef IP_ADD_MEMBERSHIP
    if(GlobusLibcSockaddrGetFamily(attr->multicast_addr) == AF_INET)
    {
        struct in_addr                  inet_address;
        struct ip_mreq                  mreq;
        globus_addrinfo_t *             addrinfo;
        globus_addrinfo_t               addrinfo_hints;
        
        if(attr->bind_address)
        {
            memset(&addrinfo_hints, 0, sizeof(globus_addrinfo_t));
            addrinfo_hints.ai_flags = GLOBUS_AI_PASSIVE;
            addrinfo_hints.ai_family = AF_INET;
            addrinfo_hints.ai_socktype = SOCK_DGRAM;
            addrinfo_hints.ai_protocol = 0;
        
            result = globus_libc_getaddrinfo(attr->bind_address,
                GLOBUS_NULL, &addrinfo_hints, &addrinfo);
            if(result != GLOBUS_SUCCESS)
            {
                result = GlobusXIOErrorWrapFailed(
                    "globus_libc_getaddrinfo", result);
                goto error_join;
            }
            
            inet_address = ((struct sockaddr_in *) addrinfo->ai_addr)->sin_addr;
            globus_libc_freeaddrinfo(addrinfo);
        }
        else
        {
            inet_address.s_addr = htonl(INADDR_ANY);
        }
        
        mreq.imr_multiaddr =
            ((struct sockaddr_in *) &attr->multicast_addr)->sin_addr;
        mreq.imr_interface = inet_address;
        
        result = globus_xio_system_socket_setsockopt(
            fd, IPPROTO_IP, IP_ADD_MEMBERSHIP, &mreq, sizeof(mreq));
        if(result != GLOBUS_SUCCESS)
        {
            goto error_join;
        }
    }
    else
#endif
#ifdef IPV6_ADD_MEMBERSHIP
    if(GlobusLibcSockaddrGetFamily(attr->multicast_addr) == AF_INET6)
    {
        struct ipv6_mreq                mreq;
        
        /**
         * XXX need to translate bind address into index
         */        
        mreq.ipv6mr_multiaddr = 
            ((struct sockaddr_in6 *) &attr->multicast_addr)->sin6_addr;
        mreq.ipv6mr_interface = 0;
        
        result = globus_xio_system_socket_setsockopt(
            fd, IPPROTO_IPV6, IPV6_ADD_MEMBERSHIP, &mreq, sizeof(mreq));
        if(result != GLOBUS_SUCCESS)
        {
            goto error_join;
        }
    }
    else
#endif
    {
        result = GlobusXIOErrorSystemError("multicast join", ENOTSUP);
        goto error_join;
    }
    
    return GLOBUS_SUCCESS;

error_join:
error_bind:
error_attrs:
    globus_xio_system_socket_close(fd);
error_socket:
    return result;
}

static
globus_result_t
globus_l_xio_udp_create_listener(
    globus_l_handle_t *                 handle,
    const globus_l_attr_t *             attr)
{
    globus_result_t                     result;
    globus_addrinfo_t *                 save_addrinfo;
    globus_addrinfo_t *                 addrinfo;
    globus_addrinfo_t                   addrinfo_hints;
    char                                portbuf[10];
    char *                              port;
    globus_xio_system_socket_t          fd;
    globus_bool_t                       try_again = GLOBUS_FALSE;
    GlobusXIOName(globus_l_xio_udp_create_listener);
    
    if(attr->listener_serv)
    {
        port = attr->listener_serv;
    }
    else
    {
        snprintf(portbuf, sizeof(portbuf), "%d", attr->listener_port);
        port = portbuf;
    }
    
    /* setup hints for types of connectable sockets we want */
    memset(&addrinfo_hints, 0, sizeof(globus_addrinfo_t));
    addrinfo_hints.ai_flags = GLOBUS_AI_PASSIVE;
    addrinfo_hints.ai_socktype = SOCK_DGRAM;
    addrinfo_hints.ai_protocol = 0;
    if(attr->bind_address)
    {
        addrinfo_hints.ai_family = attr->no_ipv6 ? PF_INET : PF_UNSPEC;
    }
    else
    {
        addrinfo_hints.ai_family = attr->no_ipv6 ? PF_INET : PF_INET6;
    }

    /* getaddrinfo now returns results in the 'correct' rfc order of ipv4 
     * first and ipv6 last, but we want to bind to ipv6 addresses first. 
     * we'll loop through the whole getaddrinfo+bind twice -- first for V6, 
     * then for V4 */
    do
    {   
        addrinfo = NULL;
         
        result = globus_libc_getaddrinfo(
            attr->bind_address, port, &addrinfo_hints, &save_addrinfo);
        if(result != GLOBUS_SUCCESS && 
            attr->listener_serv && attr->listener_port > 0)
        {
            /* it's possible the service name doesnt exist, since they also
             * specified a numeric port, lets try that one
             */
            snprintf(portbuf, sizeof(portbuf), "%d", attr->listener_port);
            result = globus_libc_getaddrinfo(
                attr->bind_address, portbuf, &addrinfo_hints, &save_addrinfo);
        }

        if(result != GLOBUS_SUCCESS)
        {
            if(addrinfo_hints.ai_family == PF_INET6)
            {
                try_again = GLOBUS_TRUE;
                addrinfo_hints.ai_family = PF_INET;
                continue;
            }
            else
            {
                result = GlobusXIOErrorWrapFailed(
                    "globus_libc_getaddrinfo", result);
                goto error_getaddrinfo;
            }
        }
    
    
        for(addrinfo = save_addrinfo; addrinfo; addrinfo = addrinfo->ai_next)
        {
            if(GlobusLibcProtocolFamilyIsIP(addrinfo->ai_family))
            {
                globus_bool_t               found = GLOBUS_FALSE;
                
                do
                {
                    result = globus_xio_system_socket_create(
                        &fd,
                        addrinfo->ai_family,
                        addrinfo->ai_socktype,
                        addrinfo->ai_protocol);
                    if(result != GLOBUS_SUCCESS)
                    {
                        break;
                    }
                    
                    if(addrinfo->ai_family == AF_INET6 && !attr->bind_address)
                    {
                        int             int_zero = 0;
                        
                        result = globus_xio_system_socket_setsockopt(
                            fd, IPPROTO_IPV6, IPV6_V6ONLY, &int_zero, sizeof(int_zero));
                        if(result != GLOBUS_SUCCESS)
                        {
                             result = GlobusXIOErrorWrapFailed(
                                "Unable to disable V6ONLY sockopt.", result);
                        }
                    }
            
                    result = globus_l_xio_udp_apply_handle_attrs(
                        attr, fd, GLOBUS_FALSE);
                    if(result != GLOBUS_SUCCESS)
                    {
                        result = GlobusXIOErrorWrapFailed(
                            "globus_l_xio_udp_apply_handle_attrs", result);
                        globus_xio_system_socket_close(fd);
                        continue;
                    }
            
                    result = globus_l_xio_udp_bind(
                        fd,
                        addrinfo->ai_addr,
                        addrinfo->ai_addrlen,
                        attr->restrict_port ? attr->listener_min_port : 0,
                        attr->restrict_port ? attr->listener_max_port : 0);
                    if(result != GLOBUS_SUCCESS)
                    {
                        result = GlobusXIOErrorWrapFailed(
                            "globus_l_xio_udp_bind", result);
                        globus_xio_system_socket_close(fd);
                        break;
                    }
                    found = GLOBUS_TRUE;
                } while(!found);
                
                if(found)
                {
                    break;
                }
            }
        }
        
        if(!addrinfo && addrinfo_hints.ai_family == PF_INET6)
        {
            try_again = GLOBUS_TRUE;
            addrinfo_hints.ai_family = PF_INET;
            globus_libc_freeaddrinfo(save_addrinfo);
        }
        else
        {
            try_again = GLOBUS_FALSE;
        }
    } while(!addrinfo && try_again);
    
    if(!addrinfo)
    {
        if(result == GLOBUS_SUCCESS)
        {
            result = GlobusXIOUdpErrorNoAddrs();
        }
        
        goto error_no_addrinfo;
    }
    
    handle->fd = fd;
    handle->socket_sa_family = addrinfo->ai_family;
    globus_libc_freeaddrinfo(save_addrinfo);

    return GLOBUS_SUCCESS;

error_no_addrinfo:
    globus_libc_freeaddrinfo(save_addrinfo);

error_getaddrinfo:
    return result;
}

static
globus_result_t
globus_l_xio_udp_handle_init(
    globus_l_handle_t **                handle,
    const globus_l_attr_t *             attr)
{
    globus_result_t                     result;
    GlobusXIOName(globus_l_xio_udp_handle_init);
    
    *handle = (globus_l_handle_t *) globus_malloc(sizeof(globus_l_handle_t));
    if(!*handle)
    {
        result = GlobusXIOErrorMemory("handle");
        goto error_handle;
    }
    
    (*handle)->connected = GLOBUS_FALSE;
    (*handle)->converted = GLOBUS_FALSE;
    (*handle)->no_ipv6 = attr->no_ipv6;
    
    return GLOBUS_SUCCESS;

error_handle:
    return result;    
}

static
void
globus_l_xio_udp_handle_destroy(
    globus_l_handle_t *                 handle)
{
    GlobusXIOName(globus_l_xio_udp_handle_destroy);
    
    free(handle);
}

static
globus_result_t
globus_l_xio_udp_connect(
    globus_l_handle_t *                 handle,
    const char *                        host,
    const char *                        port)
{
    globus_result_t                     result;
    globus_addrinfo_t *                 addrinfo;
    globus_addrinfo_t *                 save_addrinfo;
    GlobusXIOName(globus_l_xio_udp_connect);
    
    result = globus_l_xio_udp_get_addrinfo(
        host, port, &save_addrinfo, handle->no_ipv6);
    if(result != GLOBUS_SUCCESS)
    {
        result = GlobusXIOErrorWrapFailed(
            "globus_l_xio_udp_get_addrinfo", result);
        goto error_getaddrinfo;
    }
    
    for(addrinfo = save_addrinfo; addrinfo; addrinfo = addrinfo->ai_next)
    {
        if(GlobusLibcProtocolFamilyIsIP(addrinfo->ai_family))
        {
            result = globus_xio_system_socket_connect(
                handle->fd, addrinfo->ai_addr, addrinfo->ai_addrlen);
            if(result == GLOBUS_SUCCESS)
            {
                break;
            }
        }
    }
    
    if(!addrinfo)
    {
        if(result == GLOBUS_SUCCESS)
        {
            result = GlobusXIOUdpErrorNoAddrs();
        }
        
        goto error_no_addrinfo;
    }
    
    globus_libc_freeaddrinfo(save_addrinfo);
    
    handle->connected = GLOBUS_TRUE;
    
    return GLOBUS_SUCCESS;

error_no_addrinfo:
    globus_libc_freeaddrinfo(save_addrinfo);

error_getaddrinfo:
    
    return result;
}

/*
 *  open a udp
 */
static
globus_result_t
globus_l_xio_udp_open(
    const globus_xio_contact_t *        contact_info,
    void *                              driver_link,
    void *                              driver_attr,
    globus_xio_operation_t              op)
{
    globus_l_handle_t *                 handle;
    const globus_l_attr_t *             attr;
    globus_result_t                     result;
    char *                              port;
    GlobusXIOName(globus_l_xio_udp_open);
    
    attr = (globus_l_attr_t *) 
        (driver_attr ? driver_attr : &globus_l_xio_udp_attr_default);
    
    result = globus_l_xio_udp_handle_init(&handle, attr);
    if(result != GLOBUS_SUCCESS)
    {
        result = GlobusXIOErrorWrapFailed(
            "globus_l_xio_udp_handle_init", result);
        goto error_handle;
    }
    
    if(attr->fd == GLOBUS_XIO_UDP_INVALID_HANDLE)
    {
        if(attr->join_multicast)
        {
            result = globus_l_xio_udp_join_multicast(handle, attr);
            if(result != GLOBUS_SUCCESS)
            {
                result = GlobusXIOErrorWrapFailed(
                    "globus_l_xio_udp_join_multicast", result);
                goto error_listen;
            }
        }
        else
        {
            /* setup the local side */
            result = globus_l_xio_udp_create_listener(handle, attr);
            if(result != GLOBUS_SUCCESS)
            {
                result = GlobusXIOErrorWrapFailed(
                    "globus_l_xio_udp_create_listener", result);
                goto error_listen;
            }
        }
    }
    else
    {
        handle->fd = attr->fd;
        handle->converted = GLOBUS_TRUE;
        result = globus_l_xio_udp_apply_handle_attrs(
            attr, handle->fd, GLOBUS_TRUE);
        if(result != GLOBUS_SUCCESS)
        {
            result = GlobusXIOErrorWrapFailed(
                "globus_l_xio_udp_apply_handle_attrs", result);
            goto error_attrs;
        }
    }
    
    /* if a peer has been chosen, bind them */
    port = contact_info->port ? contact_info->port : contact_info->scheme;
    if(contact_info->host && port)
    {
        result = globus_l_xio_udp_connect(
            handle, contact_info->host, port);
        if(result != GLOBUS_SUCCESS)
        {
            result = GlobusXIOErrorWrapFailedWithMessage2(result,
                "Unable to connect to %s:%s",
                contact_info->host, port);
            goto error_connect;
        }
    }
    
    result = globus_xio_system_socket_init(
        &handle->system, handle->fd, GLOBUS_XIO_SYSTEM_UDP);
    if(result != GLOBUS_SUCCESS)
    {
        result = GlobusXIOErrorWrapFailed(
            "globus_xio_system_socket_init", result);
        goto error_init;
    }
    
    globus_xio_driver_finished_open(handle, op, GLOBUS_SUCCESS);
    
    return GLOBUS_SUCCESS;

error_init:
error_connect:
    if(!handle->converted)
    {
        globus_xio_system_socket_close(handle->fd);
    }
    
error_listen:
error_attrs:   
    globus_l_xio_udp_handle_destroy(handle);  

error_handle:
    return result;
}

/*
 *  close a udp
 */
static
globus_result_t
globus_l_xio_udp_close(
    void *                              driver_specific_handle,
    void *                              attr,
    globus_xio_operation_t              op)
{
    globus_l_handle_t *                 handle;
    GlobusXIOName(globus_l_xio_udp_close);

    handle = (globus_l_handle_t *) driver_specific_handle;
    
    globus_xio_system_socket_destroy(handle->system);
    
    if(!handle->converted)
    {
        globus_xio_system_socket_close(handle->fd);
    }
    
    globus_xio_driver_finished_close(op, GLOBUS_SUCCESS);
    globus_l_xio_udp_handle_destroy(handle);

    return GLOBUS_SUCCESS;
}

static
void
globus_l_xio_udp_system_read_cb(
    globus_result_t                     result,
    globus_size_t                       nbytes,
    void *                              user_arg)
{
    globus_xio_operation_t              op;
    GlobusXIOName(globus_l_xio_udp_system_read_cb);
    
    op = (globus_xio_operation_t) user_arg;
    if(result != GLOBUS_SUCCESS && globus_xio_error_is_eof(result))
    {
        /* eof not possible, zero byte packets allowed */
        result = GLOBUS_SUCCESS;
    }
    globus_xio_driver_finished_read(op, result, nbytes);
}

/*
 *  read from a udp
 */
static
globus_result_t
globus_l_xio_udp_read(
    void *                              driver_specific_handle,
    const globus_xio_iovec_t *          iovec,
    int                                 iovec_count,
    globus_xio_operation_t              op)
{
    globus_l_handle_t *                 handle;
    globus_l_attr_t *                   attr;
    globus_sockaddr_t *                 addr;
    GlobusXIOName(globus_l_xio_udp_read);

    handle = (globus_l_handle_t *) driver_specific_handle;
    
    addr = GLOBUS_NULL;
    if(!handle->connected)
    {
        attr = (globus_l_attr_t *)
            globus_xio_operation_get_data_descriptor(op, GLOBUS_TRUE);
        if(attr)
        {
            addr = &attr->addr;
            attr->use_addr = GLOBUS_TRUE;
        }
    }
    
    /* if buflen and waitfor are both 0, we behave like register select */
    if(globus_xio_operation_get_wait_for(op) == 0 &&
        (iovec_count > 1 || iovec[0].iov_len > 0))
    {
        globus_size_t                   nbytes;
        globus_result_t                 result;
        
        result = globus_xio_system_socket_read(
            handle->system,
            iovec,
            iovec_count,
            0,
            0,
            addr,
            &nbytes);
        if(result != GLOBUS_SUCCESS && globus_xio_error_is_eof(result))
        {
            /* eof not possible, zero byte packets allowed */
            result = GLOBUS_SUCCESS;
        }
        
        globus_xio_driver_finished_read(op, result, nbytes);

        return GLOBUS_SUCCESS;
    }
    else
    {
        return globus_xio_system_socket_register_read(
            op,
            handle->system,
            iovec,
            iovec_count,
            globus_xio_operation_get_wait_for(op),
            0,
            addr,
            globus_l_xio_udp_system_read_cb,
            op);
    }
}

/*
 *  write to a udp
 */
static
globus_result_t
globus_l_xio_udp_write(
    void *                              driver_specific_handle,
    const globus_xio_iovec_t *          iovec,
    int                                 iovec_count,
    globus_xio_operation_t              op)
{
    globus_l_handle_t *                 handle;
    globus_l_attr_t *                   attr;
    globus_sockaddr_t *                 addr;
    globus_sockaddr_t                   tmp_addr;
    globus_size_t                       nbytes;
    globus_result_t                     result;
    int                                 total;
    int                                 i;
    GlobusXIOName(globus_l_xio_udp_write);

    handle = (globus_l_handle_t *) driver_specific_handle;
    
    addr = GLOBUS_NULL;
    if(!handle->connected)
    {
        attr = (globus_l_attr_t *)
            globus_xio_operation_get_data_descriptor(op, GLOBUS_FALSE);
        if(attr && attr->use_addr)
        {
            addr = &attr->addr;
            if(handle->socket_sa_family != GlobusLibcSockaddrGetFamily(addr))
            {
                result = globus_libc_addr_convert_family(
                    &attr->addr,
                    &tmp_addr,
                    handle->socket_sa_family);
                if(result != GLOBUS_SUCCESS)
                {
                    result = GlobusXIOErrorWrapFailed(
                        "globus_libc_addr_convert_family", result);
                    /* continue */
                }
                addr = &tmp_addr;
            }
        }
    }

    /* for UDP sockets, this is supposed to write the entire thing at all
     * times if it fits in buffer
     */
    result = globus_xio_system_socket_write(
        handle->system, iovec, iovec_count, 0, 0, addr, &nbytes);
    if(result != GLOBUS_SUCCESS)
    {
        result = GlobusXIOErrorWrapFailed(
            "globus_xio_system_socket_write", result);
        goto error_write;
    }
    
    total = 0;
    for(i = 0; i < iovec_count; i++)
    {
        total += iovec[i].iov_len;
    }
    
    if(nbytes != total)
    {
        result = GlobusXIOUdpErrorShortWrite();
    }
    
    globus_xio_driver_finished_write(op, result, nbytes);
    /* Since I actually perform the write here and some bytes may be written
     * I have to 'finish' with any errors and return success for the request
     */
    return GLOBUS_SUCCESS;

error_write:
    return result;
}

static
globus_result_t
globus_l_xio_udp_cntl(
    void *                              driver_specific_handle,
    int                                 cmd,
    va_list                             ap)
{
    globus_l_handle_t *                 handle;
    globus_result_t                     result;
    int *                               out_int;
    int                                 in_int;
    char *                              in_string;
    globus_sockaddr_t                   sock_name;
    globus_xio_system_socket_t          fd;
    globus_socklen_t                    optlen;
    int                                 flags;
    char **                             out_string;
    globus_xio_system_socket_t *        out_fd;
    globus_xio_contact_t                contact_info;
    GlobusXIOName(globus_l_xio_udp_cntl);

    handle = (globus_l_handle_t *) driver_specific_handle;
    fd = handle->fd;
    flags = GLOBUS_LIBC_ADDR_LOCAL;
    
    switch(cmd)
    {
      /* globus_xio_system_socket_t *   fd_out */
      case GLOBUS_XIO_UDP_GET_HANDLE:
        out_fd = va_arg(ap, globus_xio_system_socket_t *);
        *out_fd = fd;
        break;
        
      /* int                            sndbuf */
      case GLOBUS_XIO_UDP_SET_SNDBUF:
        in_int = va_arg(ap, int);
        result = globus_xio_system_socket_setsockopt(
            fd, SOL_SOCKET, SO_SNDBUF, &in_int, sizeof(in_int));
        if(result != GLOBUS_SUCCESS)
        {
            goto error_sockopt;
        }
        break;
        
      /* int *                          sndbuf_out */
      case GLOBUS_XIO_UDP_GET_SNDBUF:
        out_int = va_arg(ap, int *);

        optlen = sizeof(int);

        result = globus_xio_system_socket_getsockopt(
            fd, SOL_SOCKET, SO_SNDBUF, out_int, &optlen);
        if(result != GLOBUS_SUCCESS)
        {
            goto error_sockopt;
        }
        break;
        
      /* int                            rcvbuf */
      case GLOBUS_XIO_UDP_SET_RCVBUF:
        in_int = va_arg(ap, int);
        result = globus_xio_system_socket_setsockopt(
            fd, SOL_SOCKET, SO_RCVBUF, &in_int, sizeof(in_int));
        if(result != GLOBUS_SUCCESS)
        {
            goto error_sockopt;
        }
        break;
        
      /* int *                          rcvbuf_out */
      case GLOBUS_XIO_UDP_GET_RCVBUF:
        out_int = va_arg(ap, int *);
        optlen = sizeof(int);
        result = globus_xio_system_socket_getsockopt(
            fd, SOL_SOCKET, SO_RCVBUF, out_int, &optlen);
        if(result != GLOBUS_SUCCESS)
        {
            goto error_sockopt;
        }
        break;
        
      /* char **                        contact_string_out */
      case GLOBUS_XIO_UDP_GET_NUMERIC_CONTACT:
      case GLOBUS_XIO_GET_LOCAL_NUMERIC_CONTACT:
        flags |= GLOBUS_LIBC_ADDR_NUMERIC;
        /* fall through */
      
      /* char **                        contact_string_out */
      case GLOBUS_XIO_UDP_GET_CONTACT:
      case GLOBUS_XIO_GET_LOCAL_CONTACT:
        optlen = sizeof(globus_sockaddr_t);
        result = globus_xio_system_socket_getsockname(
            fd, (struct sockaddr *) &sock_name, &optlen);
        if(result != GLOBUS_SUCCESS)
        {
            goto error_sockopt;
        }
        
        out_string = va_arg(ap, char **);
        result = globus_libc_addr_to_contact_string(
            &sock_name,
            flags,
            out_string);
        if(result != GLOBUS_SUCCESS)
        {
            result = GlobusXIOErrorWrapFailed(
                "globus_libc_addr_to_contact_string", result);
            goto error_contact;
        }
        break;
      
      /* char *                         contact_string */
      case GLOBUS_XIO_UDP_CONNECT:
        in_string = va_arg(ap, char *);
        
        result = globus_xio_contact_parse(&contact_info, in_string);
        if(result != GLOBUS_SUCCESS)
        {
            result = GlobusXIOErrorWrapFailed(
                "globus_xio_contact_parse", result);
            goto error_connect;
        }
        
        in_string = contact_info.port
            ? contact_info.port : contact_info.scheme;
        if(contact_info.host && in_string)
        {
            result = globus_l_xio_udp_connect(
                handle, contact_info.host, in_string);
            if(result != GLOBUS_SUCCESS)
            {
                result = GlobusXIOErrorWrapFailedWithMessage2(result,
                    "Unable to connect to %s:%s",
                    contact_info.host, in_string);
                globus_xio_contact_destroy(&contact_info);
                goto error_connect;
            }
            globus_xio_contact_destroy(&contact_info);
        }
        else
        {
            /* attempt to 'disconnect' socket */
            struct sockaddr_in          addr;
            
            globus_xio_contact_destroy(&contact_info);
            memset(&addr, 0, sizeof(addr));
            addr.sin_family = PF_UNSPEC;
            
            result = globus_xio_system_socket_connect(
                fd, (struct sockaddr *) &addr, sizeof(addr));
            if(result != GLOBUS_SUCCESS)
            {
                goto error_connect;
            }
            
            handle->connected = GLOBUS_FALSE;
        }
        
        break;
        
      default:
        result = GlobusXIOErrorInvalidCommand(cmd);
        goto error_invalid;
        break;
    }

    return GLOBUS_SUCCESS;

error_connect:
error_contact:
error_invalid:
error_sockopt:
    return result;
}

static
globus_result_t
globus_l_xio_udp_init(
    globus_xio_driver_t *               out_driver)
{
    globus_xio_driver_t                 driver;
    globus_result_t                     result;
    GlobusXIOName(globus_l_xio_udp_init);
    
    /* I dont support any driver options, so I'll ignore the ap */
    
    result = globus_xio_driver_init(&driver, "udp", GLOBUS_NULL);
    if(result != GLOBUS_SUCCESS)
    {
        result = GlobusXIOErrorWrapFailed(
            "globus_xio_driver_init", result);
        goto error_init;
    }

    globus_xio_driver_set_transport(
        driver,
        globus_l_xio_udp_open,
        globus_l_xio_udp_close,
        globus_l_xio_udp_read,
        globus_l_xio_udp_write,
        globus_l_xio_udp_cntl);
    
    globus_xio_driver_set_attr(
        driver,
        globus_l_xio_udp_attr_init,
        globus_l_xio_udp_attr_copy,
        globus_l_xio_udp_attr_cntl,
        globus_l_xio_udp_attr_destroy);
    
    globus_xio_driver_string_cntl_set_table(
        driver,
        udp_l_string_opts_table);

    *out_driver = driver;

    return GLOBUS_SUCCESS;

error_init:
    return result;
}

static
void
globus_l_xio_udp_destroy(
    globus_xio_driver_t                 driver)
{
    globus_xio_driver_destroy(driver);
}

GlobusXIODefineDriver(
    udp,
    globus_l_xio_udp_init,
    globus_l_xio_udp_destroy);

static
int
globus_l_xio_udp_activate(void)
{
    int                                 min;
    int                                 max;
    int                                 rc;
    GlobusXIOName(globus_l_xio_udp_activate);
    
    if(globus_l_xio_udp_get_env_pair(
        "GLOBUS_UDP_PORT_RANGE", &min, &max) && min <= max)
    {
        globus_l_xio_udp_attr_default.listener_min_port = min;
        globus_l_xio_udp_attr_default.listener_max_port = max;
    }
    
    rc = globus_module_activate(GLOBUS_XIO_SYSTEM_MODULE);
    if(rc == GLOBUS_SUCCESS)
    {
        GlobusXIORegisterDriver(udp);
    }
    return rc;
}

static
int
globus_l_xio_udp_deactivate(void)
{
    GlobusXIOName(globus_l_xio_udp_deactivate);
    
    globus_l_xio_udp_attr_default.listener_min_port = 0;
    globus_l_xio_udp_attr_default.listener_max_port = 0;
    
    GlobusXIOUnRegisterDriver(udp);
    return globus_module_deactivate(GLOBUS_XIO_SYSTEM_MODULE);
}
