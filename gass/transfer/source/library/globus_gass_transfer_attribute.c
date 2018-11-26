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

#ifndef GLOBUS_DONT_DOCUMENT_INTERNAL
/**
 * @file globus_gass_transfer_attribute.c
 * @brief Request and Listener Attributes
 *
 * This module implements the globus gass request and transfer attribute
 * accessors
 */
#endif

#include "globus_i_gass_transfer.h"
#include <string.h>

#ifndef GLOBUS_DONT_DOCUMENT_INTERNAL
/* Request Attribute Object Instance Data Types */
typedef struct
{
    char *				proxy_url;
    globus_size_t			block_size;
    globus_gass_transfer_file_mode_t	file_mode;
    globus_bool_t			connection_reuse;
} globus_gass_object_type_requestattr_instance_t;

typedef struct
{
    int					sndbuf;
    int					rcvbuf;
    globus_bool_t			nodelay;
} globus_gass_object_type_socket_requestattr_instance_t;

typedef struct
{
    globus_gass_transfer_authorization_t	authorization;
    char *					subject;
} globus_gass_object_type_secure_requestattr_instance_t;

/* Listener Attribute Object Types */

typedef struct
{
    int						backlog;
    unsigned short				port;
} globus_gass_object_type_listenerattr_instance_t;

/* Module-Specific Prototypes */
static
void
globus_l_gass_requestattr_copy(
    void *				src_data,
    void **				dst_data);

static
void
globus_l_gass_requestattr_destroy(
    void *				data);

static
void
globus_l_gass_socket_requestattr_copy(
    void *				src_data,
    void **				dst_data);

static
void
globus_l_gass_socket_requestattr_destroy(
    void *				data);

static
void
globus_l_gass_secure_requestattr_copy(
    void *				src_data,
    void **				dst_data);

static
void
globus_l_gass_secure_requestattr_destroy(
    void *				data);

static
void
globus_l_gass_listenerattr_copy(
    void *				src_data,
    void **				dst_data);

static
void
globus_l_gass_listenerattr_destroy(
    void *				data);
#endif

/* Object Type Declarations */
const globus_object_type_t
GLOBUS_GASS_OBJECT_TYPE_REQUESTATTR_DEFINITION =
globus_object_type_static_initializer(
    GLOBUS_OBJECT_TYPE_BASE,
    globus_l_gass_requestattr_copy,
    globus_l_gass_requestattr_destroy,
    GLOBUS_NULL /* class data */);

const globus_object_type_t
GLOBUS_GASS_OBJECT_TYPE_SOCKET_REQUESTATTR_DEFINITION =
globus_object_type_static_initializer(
    GLOBUS_GASS_OBJECT_TYPE_REQUESTATTR,
    globus_l_gass_socket_requestattr_copy,
    globus_l_gass_socket_requestattr_destroy,
    GLOBUS_NULL /* class data */);

const globus_object_type_t
GLOBUS_GASS_OBJECT_TYPE_SECURE_REQUESTATTR_DEFINITION =
globus_object_type_static_initializer(
    GLOBUS_GASS_OBJECT_TYPE_SOCKET_REQUESTATTR,
    globus_l_gass_secure_requestattr_copy,
    globus_l_gass_secure_requestattr_destroy,
    GLOBUS_NULL /* class data */);

const globus_object_type_t
GLOBUS_GASS_OBJECT_TYPE_LISTENERATTR_DEFINITION =
globus_object_type_static_initializer(
    GLOBUS_OBJECT_TYPE_BASE,
    globus_l_gass_listenerattr_copy,
    globus_l_gass_listenerattr_destroy,
    GLOBUS_NULL /* class data */);

/* API Functions */

/**
 * Initialize a base request attribute.
 * @ingroup globus_gass_transfer_requestattr_implementation
 *
 * @param obj
 * @param proxy_url
 * @param block_size
 * @param file_mode
 * @param connection_reuse
 *
 * @return Returns the @a obj pointer if the object inherited from the
 *         @a GLOBUS_GASS_OBJECT_TYPE_REQUESTATTR type and the attribute
 *         could be initialized; GLOBUS_NULL otherwise.
 */
globus_object_t *
globus_gass_transfer_requestattr_initialize(
    globus_object_t *				obj,
    char *				        proxy_url,
    globus_size_t				block_size,
    globus_gass_transfer_file_mode_t		file_mode,
    globus_bool_t				connection_reuse)
{
    globus_gass_object_type_requestattr_instance_t *
						instance;
    globus_object_t *				tmpobj;

    tmpobj = globus_object_upcast(obj,
			       GLOBUS_GASS_OBJECT_TYPE_REQUESTATTR);
    if(tmpobj == GLOBUS_NULL)
    {
	return GLOBUS_NULL;
    }
    instance = globus_malloc(
	sizeof(globus_gass_object_type_requestattr_instance_t));
    if(instance == GLOBUS_NULL)
    {
	return GLOBUS_NULL;
    }
    if(proxy_url)
    {
	instance->proxy_url = globus_libc_strdup(proxy_url);
    }
    else
    {
	instance->proxy_url = GLOBUS_NULL;
    }
    instance->block_size = block_size;
    instance->file_mode = file_mode;
    instance->connection_reuse = connection_reuse;

    globus_object_set_local_instance_data(tmpobj,
					  instance);
    return obj;
}
/* globus_gass_transfer_requestattr_initialize() */

/**
 * Initialize a request attribute.
 * @ingroup globus_gass_transfer_requestattr
 *
 * This function initializes the @a attr to contain a new protocol-specific
 * request attribute.
 *
 * @param attr
 *        The attribute set to be initialized.
 * @param url_scheme
 *        The scheme which which the attribute will be used for.
 *
 * @retval GLOBUS_SUCCESS
 *         The attribute was successfully initialized.
 * @retval GLOBUS_GASS_TRANSFER_ERROR_NULL_POINTER
 *         Either @a attr or @a url_scheme was GLOBUS_NULL.
 * @retval GLOBUS_GASS_TRANSFER_ERROR_NOT_IMPLEMENTED
 *         No protocol module currently registered with GASS Transfer
 *         Library handles URLs with the specified @a url_scheme.
 */
int
globus_gass_transfer_requestattr_init(
    globus_gass_transfer_requestattr_t *	attr,
    char *				        url_scheme)
{
    globus_gass_transfer_proto_descriptor_t *	protocol;

    if(attr == GLOBUS_NULL ||
       url_scheme == GLOBUS_NULL)
    {
	return GLOBUS_GASS_TRANSFER_ERROR_NULL_POINTER;
    }

    globus_i_gass_transfer_lock();
    protocol = (globus_gass_transfer_proto_descriptor_t *)
	globus_hashtable_lookup(&globus_i_gass_transfer_protocols,
				(void *) url_scheme);
    if(protocol == GLOBUS_NULL)
    {
	globus_i_gass_transfer_unlock();
	return GLOBUS_GASS_TRANSFER_ERROR_NOT_IMPLEMENTED;
    }
    if(protocol->new_requestattr == GLOBUS_NULL)
    {
	globus_i_gass_transfer_unlock();
	return GLOBUS_GASS_TRANSFER_ERROR_NOT_IMPLEMENTED;
    }
    *attr = protocol->new_requestattr(url_scheme);
    globus_i_gass_transfer_unlock();

    return GLOBUS_SUCCESS;
}
/* globus_gass_transfer_requestattr_init() */

/**
 * Destroy a request attribute.
 * @ingroup globus_gass_transfer_requestattr
 *
 * This function destroys the attribute set specified in @a attr.
 *
 * @param attr
 *        The attribute set to be destroyed.
 *
 * @retval GLOBUS_SUCCESS
 *         The attribute was successfully destroyed.
 * @retval GLOBUS_GASS_TRANSFER_ERROR_NULL_POINTER
 *         The @a attr was GLOBUS_NULL.
 */
int
globus_gass_transfer_requestattr_destroy(
    globus_gass_transfer_requestattr_t *	attr)
{
    if(attr == GLOBUS_NULL)
    {
	return GLOBUS_GASS_TRANSFER_ERROR_NULL_POINTER;
    }
    else if (*attr == GLOBUS_NULL)
    {
	return GLOBUS_SUCCESS;
    }
    else
    {
	globus_object_free(*attr);
	return GLOBUS_SUCCESS;
    }
}
/* globus_gass_transfer_requestattr_destroy() */

/**
 * @name Proxy Server 
 */
/* @{ */
/**
 * Set/Get the proxy server attribute for a GASS transfer attribute set.
 * @ingroup globus_gass_transfer_requestattr
 *
 * This attribute allows the user to use a proxy server to handle a 
 * URL request.
 *
 * @param attr
 *        The attribute set to be modified
 * @param proxy_url
 *        The new value of the proxy_url attribute. This may be GLOBUS_NULL
 *        if no proxy is to be used to access URLs with this attribute
 *        set.
 *
 * @retval GLOBUS_SUCCESS
 *         The attribute was successfully updated.
 * @retval GLOBUS_GASS_TRANSFER_ERROR_NULL_POINTER
 *         The @a attr was GLOBUS_NULL.
 */
int
globus_gass_transfer_requestattr_set_proxy_url(
    globus_gass_transfer_requestattr_t *	attr,
    char *					proxy_url)
{
    globus_object_t *				obj;
    globus_gass_object_type_requestattr_instance_t *
						instance;

    obj = globus_object_upcast(*attr,
			       GLOBUS_GASS_OBJECT_TYPE_REQUESTATTR);
    if(obj == GLOBUS_NULL)
    {
	return GLOBUS_GASS_TRANSFER_ERROR_NULL_POINTER;
    }
    instance = globus_object_get_local_instance_data(obj);
    if(instance == GLOBUS_NULL)
    {
	return GLOBUS_GASS_TRANSFER_ERROR_NULL_POINTER;
    }
    if(instance->proxy_url)
    {
	globus_free(instance->proxy_url);
    }
    if(proxy_url)
    {
	instance->proxy_url = globus_libc_strdup(proxy_url);
    }
    else
    {
	instance->proxy_url = GLOBUS_NULL;
    }

    return GLOBUS_SUCCESS;
}
/* globus_gass_transfer_requestattr_set_proxy_url() */

int
globus_gass_transfer_requestattr_get_proxy_url(
    globus_gass_transfer_requestattr_t *	attr,
    char **					proxy_url)
{
    globus_object_t *				obj;
    globus_gass_object_type_requestattr_instance_t *
						instance;

    obj = globus_object_upcast(*attr,
			       GLOBUS_GASS_OBJECT_TYPE_REQUESTATTR);
    if(obj == GLOBUS_NULL)
    {
	return GLOBUS_GASS_TRANSFER_ERROR_NULL_POINTER;
    }
    instance = globus_object_get_local_instance_data(obj);
    if(instance == GLOBUS_NULL)
    {
	return GLOBUS_GASS_TRANSFER_ERROR_NULL_POINTER;
    }
    if(proxy_url == GLOBUS_NULL)
    {
	return GLOBUS_GASS_TRANSFER_ERROR_NULL_POINTER;
    }
    *proxy_url = instance->proxy_url;

    return GLOBUS_SUCCESS;
}
/* globus_gass_transfer_requestattr_get_proxy_url() */
/* @} */

/**
 * @name Block Size
 */
/* @{ */

/**
 * Set/Get the block size attribute for a GASS transfer attribute set.
 * @ingroup globus_gass_transfer_requestattr
 *
 * This attribute allows the user to suggest a preferred block size of
 * a server to handle a URL request.
 *
 * @param attr
 *        The attribute set to query or modify.
 * @param block_size
 *        The data block size that should be used to process requests
 *        with this attribute set.
 *
 * @retval GLOBUS_SUCCESS
 *         The attribute was successfully updated.
 * @retval GLOBUS_GASS_TRANSFER_ERROR_NULL_POINTER
 *         The @a attr was GLOBUS_NULL.
 */
int
globus_gass_transfer_requestattr_set_block_size(
    globus_gass_transfer_requestattr_t *	attr,
    globus_size_t				block_size)
{
    globus_object_t *				obj;

    globus_gass_object_type_requestattr_instance_t *
						instance;

    obj = globus_object_upcast(*attr,
			       GLOBUS_GASS_OBJECT_TYPE_REQUESTATTR);
    if(obj == GLOBUS_NULL)
    {
	return GLOBUS_GASS_TRANSFER_ERROR_NULL_POINTER;
    }
    instance = globus_object_get_local_instance_data(obj);
    if(instance == GLOBUS_NULL)
    {
	return GLOBUS_GASS_TRANSFER_ERROR_NULL_POINTER;
    }
    instance->block_size = block_size;

    return GLOBUS_SUCCESS;
}
/* globus_gass_transfer_requestattr_set_block_size() */

int
globus_gass_transfer_requestattr_get_block_size(
    globus_gass_transfer_requestattr_t *	attr,
    globus_size_t *				block_size)
{
    globus_object_t *				obj;
    globus_gass_object_type_requestattr_instance_t *
						instance;

    obj = globus_object_upcast(*attr,
			       GLOBUS_GASS_OBJECT_TYPE_REQUESTATTR);
    if(obj == GLOBUS_NULL)
    {
	return GLOBUS_GASS_TRANSFER_ERROR_NULL_POINTER;
    }
    instance = globus_object_get_local_instance_data(obj);
    if(instance == GLOBUS_NULL)
    {
	return GLOBUS_GASS_TRANSFER_ERROR_NULL_POINTER;
    }
    if(block_size == GLOBUS_NULL)
    {
	return GLOBUS_GASS_TRANSFER_ERROR_NULL_POINTER;
    }
    *block_size = instance->block_size;

    return GLOBUS_SUCCESS;
}
/* globus_gass_transfer_requestattr_get_block_size() */
/* @} */

/**
 * @name File Mode
 */
/* @{ */
/**
 * Set/Get the file mode attribute for a GASS transfer attribute set.
 * @ingroup globus_gass_transfer_requestattr
 *
 * This attribute allows the user to control whether the file will be
 * transferred in ASCII or binary file mode.
 *
 * @param attr
 *        The attribute set to query or modify.
 * @param file_mode
 *        The value of the file mode attribute.
 *
 * @retval GLOBUS_SUCCESS
 *         The attribute was successfully updated.
 * @retval GLOBUS_GASS_TRANSFER_ERROR_NULL_POINTER
 *         The @a attr was GLOBUS_NULL.
 */
int
globus_gass_transfer_requestattr_set_file_mode(
    globus_gass_transfer_requestattr_t *	attr,
    globus_gass_transfer_file_mode_t		file_mode)
{
    globus_object_t *				obj;
    globus_gass_object_type_requestattr_instance_t *
						instance;

    obj = globus_object_upcast(*attr,
			       GLOBUS_GASS_OBJECT_TYPE_REQUESTATTR);
    if(obj == GLOBUS_NULL)
    {
	return GLOBUS_GASS_TRANSFER_ERROR_NULL_POINTER;
    }
    instance = globus_object_get_local_instance_data(obj);
    if(instance == GLOBUS_NULL)
    {
	return GLOBUS_GASS_TRANSFER_ERROR_NULL_POINTER;
    }
    instance->file_mode = file_mode;

    return GLOBUS_SUCCESS;
}
/* globus_gass_transfer_requestattr_set_file_mode() */

int
globus_gass_transfer_requestattr_get_file_mode(
    globus_gass_transfer_requestattr_t *	attr,
    globus_gass_transfer_file_mode_t *		file_mode)
{
    globus_object_t *				obj;
    globus_gass_object_type_requestattr_instance_t *
						instance;

    obj = globus_object_upcast(*attr,
			       GLOBUS_GASS_OBJECT_TYPE_REQUESTATTR);
    if(obj == GLOBUS_NULL)
    {
	return GLOBUS_GASS_TRANSFER_ERROR_NULL_POINTER;
    }
    instance = globus_object_get_local_instance_data(obj);
    if(instance == GLOBUS_NULL)
    {
	return GLOBUS_GASS_TRANSFER_ERROR_NULL_POINTER;
    }
    if(file_mode == GLOBUS_NULL)
    {
	return GLOBUS_GASS_TRANSFER_ERROR_NULL_POINTER;
    }
    *file_mode = instance->file_mode;

    return GLOBUS_SUCCESS;
}
/* globus_gass_transfer_requestattr_get_file_mode() */
/* @} */

/**
 * @name Connection Reuse
 */
/* @{ */
/**
 * Set/Get the connection reuse attribute for a GASS transfer attribute set.
 * @ingroup globus_gass_transfer_requestattr
 *
 * This attribute allows the user to control whether the connection
 * associated with a GASS Transfer request should be reused after the
 * file transfer has completed.
 *
 * @param attr
 *        The attribute set to query or modify.
 * @param connection_reuse
 *        The value of the connection reuse attribute.
 *
 * @retval GLOBUS_SUCCESS
 *         The attribute was successfully updated.
 * @retval GLOBUS_GASS_TRANSFER_ERROR_NULL_POINTER
 *         The @a attr was GLOBUS_NULL.
 */
int
globus_gass_transfer_requestattr_set_connection_reuse(
    globus_gass_transfer_requestattr_t *	attr,
    globus_bool_t				connection_reuse)
{
    globus_object_t *				obj;
    globus_gass_object_type_requestattr_instance_t *
						instance;

    obj = globus_object_upcast(*attr,
			       GLOBUS_GASS_OBJECT_TYPE_REQUESTATTR);
    if(obj == GLOBUS_NULL)
    {
	return GLOBUS_GASS_TRANSFER_ERROR_NULL_POINTER;
    }
    instance = globus_object_get_local_instance_data(obj);
    if(instance == GLOBUS_NULL)
    {
	return GLOBUS_GASS_TRANSFER_ERROR_NULL_POINTER;
    }
    instance->connection_reuse = connection_reuse;

    return GLOBUS_SUCCESS;
}
/* globus_gass_transfer_requestattr_set_connection_reuse() */

int
globus_gass_transfer_requestattr_get_connection_reuse(
    globus_gass_transfer_requestattr_t *	attr,
    globus_bool_t *				connection_reuse)
{
    globus_object_t *				obj;
    globus_gass_object_type_requestattr_instance_t *
						instance;

    obj = globus_object_upcast(*attr,
			       GLOBUS_GASS_OBJECT_TYPE_REQUESTATTR);
    if(obj == GLOBUS_NULL)
    {
	return GLOBUS_GASS_TRANSFER_ERROR_NULL_POINTER;
    }
    instance = globus_object_get_local_instance_data(obj);
    if(instance == GLOBUS_NULL)
    {
	return GLOBUS_GASS_TRANSFER_ERROR_NULL_POINTER;
    }
    if(connection_reuse == GLOBUS_NULL)
    {
	return GLOBUS_GASS_TRANSFER_ERROR_NULL_POINTER;
    }
    *connection_reuse = instance->connection_reuse;

    return GLOBUS_SUCCESS;
}
/* globus_gass_transfer_requestattr_get_connection_reuse() */
/* @} */

/**
 * Initialize a socket request attribute.
 * @ingroup globus_gass_transfer_requestattr_implementation
 *
 * @param obj
 * @param proxy_url
 * @param block_size
 * @param file_mode
 * @param connection_reuse
 * @param sndbuf
 * @param rcvbuf
 * @param nodelay
 *
 * @return Returns the @a obj pointer if the object inherited from the
 *         @a GLOBUS_GASS_OBJECT_TYPE_SOCKET_REQUESTATTR type and the
 *         attribute could be initialized; GLOBUS_NULL otherwise.
 */
globus_object_t *
globus_gass_transfer_socket_requestattr_initialize(
    globus_object_t *				obj,
    char *					proxy_url,
    globus_size_t				block_size,
    globus_gass_transfer_file_mode_t		file_mode,
    globus_bool_t				connection_reuse,
    int						sndbuf,
    int						rcvbuf,
    globus_bool_t				nodelay)
{
    globus_gass_object_type_socket_requestattr_instance_t *
						instance;
    globus_object_t *				tmpobj;

    tmpobj = globus_object_upcast(obj,
				  GLOBUS_GASS_OBJECT_TYPE_SOCKET_REQUESTATTR);
    if(tmpobj == GLOBUS_NULL)
    {
	return GLOBUS_NULL;
    }
    instance = globus_malloc(
	sizeof(globus_gass_object_type_socket_requestattr_instance_t));
    if(instance == GLOBUS_NULL)
    {
	return GLOBUS_NULL;
    }
    instance->sndbuf = sndbuf;
    instance->rcvbuf = rcvbuf;
    instance->nodelay = nodelay;

    globus_object_set_local_instance_data(tmpobj,
					  instance);
    return globus_gass_transfer_requestattr_initialize(
	obj,
	proxy_url,
	block_size,
	file_mode,
	connection_reuse);
}
/* globus_gass_transfer_socket_requestattr_initialize() */

/**
 * @name Socket Send Buffer Size
 * */
/* @{ */
/**
 * Set/Get the send buffer size attribute for a GASS transfer attribute set.
 * @ingroup globus_gass_transfer_requestattr
 *
 * This attribute allows the user to control the socket send buffer
 * associated with a GASS Transfer request should be reused after the
 * file transfer has completed.
 *
 * @param attr
 *        The attribute set to query or modify.
 * @param sndbuf
 *        The value of the socket buffer.
 *
 * @retval GLOBUS_SUCCESS
 *         The attribute was successfully updated.
 * @retval GLOBUS_GASS_TRANSFER_ERROR_NULL_POINTER
 *         The @a attr was GLOBUS_NULL.
 */
int
globus_gass_transfer_requestattr_set_socket_sndbuf(
    globus_gass_transfer_requestattr_t *	attr,
    int						sndbuf)
{
    globus_object_t *				obj;
    globus_gass_object_type_socket_requestattr_instance_t *
						instance;

    obj = globus_object_upcast(*attr,
			       GLOBUS_GASS_OBJECT_TYPE_SOCKET_REQUESTATTR);
    if(obj == GLOBUS_NULL)
    {
	return GLOBUS_GASS_TRANSFER_ERROR_NULL_POINTER;
    }
    instance = globus_object_get_local_instance_data(obj);
    if(instance == GLOBUS_NULL)
    {
	return GLOBUS_GASS_TRANSFER_ERROR_NULL_POINTER;
    }
    instance->sndbuf = sndbuf;

    return GLOBUS_SUCCESS;
}
/* globus_gass_transfer_requestattr_set_socket_sndbuf() */

int
globus_gass_transfer_requestattr_get_socket_sndbuf(
    globus_gass_transfer_requestattr_t *	attr,
    int *					sndbuf)
{
    globus_object_t *				obj;
    globus_gass_object_type_socket_requestattr_instance_t *
						instance;

    obj = globus_object_upcast(*attr,
			       GLOBUS_GASS_OBJECT_TYPE_SOCKET_REQUESTATTR);
    if(obj == GLOBUS_NULL)
    {
	return GLOBUS_GASS_TRANSFER_ERROR_NULL_POINTER;
    }
    instance = globus_object_get_local_instance_data(obj);
    if(instance == GLOBUS_NULL)
    {
	return GLOBUS_GASS_TRANSFER_ERROR_NULL_POINTER;
    }
    if(sndbuf == GLOBUS_NULL)
    {
	return GLOBUS_GASS_TRANSFER_ERROR_NULL_POINTER;
    }
    *sndbuf = instance->sndbuf;

    return GLOBUS_SUCCESS;
}
/* globus_gass_transfer_requestattr_get_socket_sndbuf() */
/* @} */

/**
 * @name Receive Socket Buffer
 * */
/* @{ */
/**
 * Set/Get the receive buffer size attribute for a GASS transfer attribute set.
 * @ingroup globus_gass_transfer_requestattr
 *
 * This attribute allows the user to control the socket receive buffer
 * associated with a GASS Transfer request should be reused after the
 * file transfer has completed.
 *
 * @param attr
 *        The attribute set to query or modify.
 * @param rcvbuf
 *        The value of the socket buffer.
 *
 * @retval GLOBUS_SUCCESS
 *         The attribute was successfully updated.
 * @retval GLOBUS_GASS_TRANSFER_ERROR_NULL_POINTER
 *         The @a attr was GLOBUS_NULL.
 */
int
globus_gass_transfer_requestattr_set_socket_rcvbuf(
    globus_gass_transfer_requestattr_t *	attr,
    int						rcvbuf)
{
    globus_object_t *				obj;
    globus_gass_object_type_socket_requestattr_instance_t *
						instance;

    obj = globus_object_upcast(*attr,
			       GLOBUS_GASS_OBJECT_TYPE_SOCKET_REQUESTATTR);
    if(obj == GLOBUS_NULL)
    {
	return GLOBUS_GASS_TRANSFER_ERROR_NULL_POINTER;
    }
    instance = globus_object_get_local_instance_data(obj);
    if(instance == GLOBUS_NULL)
    {
	return GLOBUS_GASS_TRANSFER_ERROR_NULL_POINTER;
    }
    instance->rcvbuf = rcvbuf;

    return GLOBUS_SUCCESS;
}
/* globus_gass_transfer_requestattr_set_socket_rcvbuf() */

int
globus_gass_transfer_requestattr_get_socket_rcvbuf(
    globus_gass_transfer_requestattr_t *	attr,
    int *					rcvbuf)
{
    globus_object_t *				obj;
    globus_gass_object_type_socket_requestattr_instance_t *
						instance;

    obj = globus_object_upcast(*attr,
			       GLOBUS_GASS_OBJECT_TYPE_SOCKET_REQUESTATTR);
    if(obj == GLOBUS_NULL)
    {
	return GLOBUS_GASS_TRANSFER_ERROR_NULL_POINTER;
    }
    instance = globus_object_get_local_instance_data(obj);
    if(instance == GLOBUS_NULL)
    {
	return GLOBUS_GASS_TRANSFER_ERROR_NULL_POINTER;
    }
    if(rcvbuf == GLOBUS_NULL)
    {
	return GLOBUS_GASS_TRANSFER_ERROR_NULL_POINTER;
    }
    *rcvbuf = instance->rcvbuf;

    return GLOBUS_SUCCESS;
}
/* globus_gass_transfer_requestattr_get_socket_rcvbuf() */
/* @} */

/**
 * @name TCP Nodelay
 */
/* @{ */
/**
 * Set/Get the TCP nodelay attribute for a GASS transfer attribute set.
 * @ingroup globus_gass_transfer_requestattr
 *
 * This attribute allows the user to control the socket receive buffer
 * associated with a GASS Transfer request should be reused after the
 * file transfer has completed.
 *
 * @param attr
 *        The attribute set to query or modify.
 * @param nodelay
 *        The value of the nodelay attribute.
 *
 * @retval GLOBUS_SUCCESS
 *         The attribute was successfully updated.
 * @retval GLOBUS_GASS_TRANSFER_ERROR_NULL_POINTER
 *         The @a attr was GLOBUS_NULL.
 */
int
globus_gass_transfer_requestattr_set_socket_nodelay(
    globus_gass_transfer_requestattr_t *	attr,
    globus_bool_t				nodelay)
{
    globus_object_t *				obj;
    globus_gass_object_type_socket_requestattr_instance_t *
						instance;

    obj = globus_object_upcast(*attr,
			       GLOBUS_GASS_OBJECT_TYPE_SOCKET_REQUESTATTR);
    if(obj == GLOBUS_NULL)
    {
	return GLOBUS_GASS_TRANSFER_ERROR_NULL_POINTER;
    }
    instance = globus_object_get_local_instance_data(obj);
    if(instance == GLOBUS_NULL)
    {
	return GLOBUS_GASS_TRANSFER_ERROR_NULL_POINTER;
    }
    instance->nodelay = nodelay;

    return GLOBUS_SUCCESS;
}
/* globus_gass_transfer_requestattr_set_socket_nodelay() */

int
globus_gass_transfer_requestattr_get_socket_nodelay(
    globus_gass_transfer_requestattr_t *	attr,
    globus_bool_t *				nodelay)
{
    globus_object_t *				obj;
    globus_gass_object_type_socket_requestattr_instance_t *
						instance;

    obj = globus_object_upcast(*attr,
			       GLOBUS_GASS_OBJECT_TYPE_SOCKET_REQUESTATTR);
    if(obj == GLOBUS_NULL)
    {
	return GLOBUS_GASS_TRANSFER_ERROR_NULL_POINTER;
    }
    instance = globus_object_get_local_instance_data(obj);
    if(instance == GLOBUS_NULL)
    {
	return GLOBUS_GASS_TRANSFER_ERROR_NULL_POINTER;
    }
    if(nodelay == GLOBUS_NULL)
    {
	return GLOBUS_GASS_TRANSFER_ERROR_NULL_POINTER;
    }
    *nodelay = instance->nodelay;

    return GLOBUS_SUCCESS;
}
/* globus_gass_transfer_requestattr_get_socket_nodelay() */
/* @} */

/**
 * Initialize a secure request attribute.
 * @ingroup globus_gass_transfer_requestattr_implementation
 *
 * @param obj
 * @param proxy_url
 * @param block_size
 * @param file_mode
 * @param connection_reuse
 * @param sndbuf
 * @param rcvbuf
 * @param nodelay
 * @param authorization
 * @param subject
 *
 * @return Returns the @a obj pointer if the object inherited from the
 *         @a GLOBUS_GASS_OBJECT_TYPE_SECURE_REQUESTATTR type and the
 *         attribute could be initialized; GLOBUS_NULL otherwise.
 */
globus_object_t *
globus_gass_transfer_secure_requestattr_initialize(
    globus_object_t *				obj,
    char *					proxy_url,
    globus_size_t				block_size,
    globus_gass_transfer_file_mode_t		file_mode,
    globus_bool_t				connection_reuse,
    int						sndbuf,
    int						rcvbuf,
    globus_bool_t				nodelay,
    globus_gass_transfer_authorization_t	authorization,
    char *					subject)
{
    globus_gass_object_type_secure_requestattr_instance_t *
						instance;
    globus_object_t *				tmpobj;

    tmpobj = globus_object_upcast(obj,
			       GLOBUS_GASS_OBJECT_TYPE_SECURE_REQUESTATTR);
    if(tmpobj == GLOBUS_NULL)
    {
	return GLOBUS_NULL;
    }
    instance = globus_malloc(
	sizeof(globus_gass_object_type_secure_requestattr_instance_t));
    if(instance == GLOBUS_NULL)
    {
	return GLOBUS_NULL;
    }
    instance->authorization = GLOBUS_GASS_TRANSFER_AUTHORIZE_SELF;
    instance->subject = GLOBUS_NULL;

    globus_object_set_local_instance_data(tmpobj,
					  instance);
    return globus_gass_transfer_socket_requestattr_initialize(
	obj,
	proxy_url,
	block_size,
	file_mode,
	connection_reuse,
	sndbuf,
	rcvbuf,
	nodelay);
}
/* globus_gass_transfer_secure_requestattr_initialize() */

/**
 * @name Authorization
 */
/* @{ */
/**
 * Set/Get the authorization attribute for a GASS transfer attribute set.
 * @ingroup globus_gass_transfer_requestattr
 *
 * This attribute allows the user to control what type of authorization
 * should be done when GASS Transfer requests are processed.
 *
 * @param attr
 *        The attribute set to query or modify.
 * @param mode
 *        The authorization mode to use.
 * @param subject
 *        The subject name of the authorized subject, if @a mode is
 *        GLOBUS_GASS_TRANSFER_AUTHORIZE_SUBJECT
 *
 * @retval GLOBUS_SUCCESS
 *         The attribute was successfully updated.
 * @retval GLOBUS_GASS_TRANSFER_ERROR_NULL_POINTER
 *         The @a attr was GLOBUS_NULL.
 */
int
globus_gass_transfer_secure_requestattr_set_authorization(
    globus_gass_transfer_requestattr_t *	attr,
    globus_gass_transfer_authorization_t	mode,
    char *					subject)
{
    globus_object_t *				obj;
    globus_gass_object_type_secure_requestattr_instance_t *
						instance;

    obj = globus_object_upcast(*attr,
			       GLOBUS_GASS_OBJECT_TYPE_SECURE_REQUESTATTR);
    if(obj == GLOBUS_NULL)
    {
	return GLOBUS_GASS_TRANSFER_ERROR_NULL_POINTER;
    }
    instance = globus_object_get_local_instance_data(obj);
    if(instance == GLOBUS_NULL)
    {
	return GLOBUS_GASS_TRANSFER_ERROR_NULL_POINTER;
    }
    instance->authorization = mode;
    if(instance->subject)
    {
	globus_free(instance->subject);
    }
    if(subject)
    {
	instance->subject = globus_libc_strdup(subject);
    }
    else
    {
	instance->subject = GLOBUS_NULL;
    }

    return GLOBUS_SUCCESS;
}
/* globus_gass_transfer_secure_requestattr_set_authorization() */

int
globus_gass_transfer_secure_requestattr_get_authorization(
    globus_gass_transfer_requestattr_t *	attr,
    globus_gass_transfer_authorization_t *	mode,
    char **					subject)
{
    globus_object_t *				obj;
    globus_gass_object_type_secure_requestattr_instance_t *
						instance;

    obj = globus_object_upcast(*attr,
			       GLOBUS_GASS_OBJECT_TYPE_SECURE_REQUESTATTR);
    if(obj == GLOBUS_NULL)
    {
	return GLOBUS_GASS_TRANSFER_ERROR_NULL_POINTER;
    }
    instance = globus_object_get_local_instance_data(obj);
    if(instance == GLOBUS_NULL)
    {
	return GLOBUS_GASS_TRANSFER_ERROR_NULL_POINTER;
    }
    if(mode == GLOBUS_NULL ||
       subject == GLOBUS_NULL)
    {
	return GLOBUS_GASS_TRANSFER_ERROR_NULL_POINTER;
    }
    *mode = instance->authorization;
    *subject = instance->subject;

    return GLOBUS_SUCCESS;
}
/* globus_gass_transfer_secure_requestattr_get_authorization() */
/* @} */

/**
 * Initialize a base listener attribute.
 * @ingroup globus_gass_transfer_requestattr_implementation
 *
 * @param obj
 * @param backlog
 * @param port
 *
 * @return Returns the @a obj pointer if the object inherited from the
 *         @a GLOBUS_GASS_OBJECT_TYPE_LISTENERTATTR type and the
 *         attribute could be initialized; GLOBUS_NULL otherwise.
 */
globus_object_t *
globus_gass_transfer_listenerattr_initialize(
    globus_object_t *				obj,
    int						backlog,
    unsigned short				port)
{
    globus_gass_object_type_listenerattr_instance_t *
						instance;

    obj = globus_object_upcast(obj,
			       GLOBUS_GASS_OBJECT_TYPE_LISTENERATTR);
    if(obj == GLOBUS_NULL)
    {
	return GLOBUS_NULL;
    }
    instance = globus_malloc(
	sizeof(globus_gass_object_type_listenerattr_instance_t));
    if(instance == GLOBUS_NULL)
    {
	return GLOBUS_NULL;
    }
    instance->backlog = backlog;
    instance->port = port;

    globus_object_set_local_instance_data(obj,
					  instance);
    return obj;
}
/* globus_gass_transfer_listenerattr_initialize() */

/**
 * Initialize a listener attribute.
 * @ingroup globus_gass_transfer_listenerattr
 *
 * This function initializes the @a attr to contain a new protocol-specific
 * listener attribute.
 *
 * @param attr
 *        The attribute set to be initialized.
 * @param url_scheme
 *        The scheme which which the attribute will be used for.
 *
 * @retval GLOBUS_SUCCESS
 *         The attribute was successfully initialized.
 * @retval GLOBUS_GASS_TRANSFER_ERROR_NULL_POINTER
 *         Either @a attr or @a url_scheme was GLOBUS_NULL.
 * @retval GLOBUS_GASS_TRANSFER_ERROR_NOT_IMPLEMENTED
 *         No protocol module currently registered with GASS Transfer
 *         Library handles URLs with the specified @a url_scheme.
 */
int
globus_gass_transfer_listenerattr_init(
    globus_gass_transfer_listenerattr_t *	attr,
    char *					url_scheme)
{
    globus_gass_transfer_proto_descriptor_t *	protocol;

    if(attr == GLOBUS_NULL)
    {
	return GLOBUS_GASS_TRANSFER_ERROR_NULL_POINTER;
    }

    globus_i_gass_transfer_lock();
    protocol = (globus_gass_transfer_proto_descriptor_t *)
	globus_hashtable_lookup(&globus_i_gass_transfer_protocols,
				(void *) url_scheme);
    if(protocol == GLOBUS_NULL)
    {
	globus_i_gass_transfer_unlock();
	return GLOBUS_GASS_TRANSFER_ERROR_NOT_IMPLEMENTED;
    }
    if(protocol->new_listenerattr == GLOBUS_NULL)
    {
	globus_i_gass_transfer_unlock();
	return GLOBUS_GASS_TRANSFER_ERROR_NOT_IMPLEMENTED;
    }
    *attr = protocol->new_listenerattr(url_scheme);
    globus_i_gass_transfer_unlock();

    return GLOBUS_SUCCESS;
}
/* globus_gass_transfer_listenerattr_init() */

/**
 * @name Listener Backlog
 */
/* @{ */
/**
 * Set/Get the backlog attribute for a GASS transfer attribute set.
 * @ingroup globus_gass_transfer_listenerattr
 *
 * This attribute allows the user to control then number of pending
 * connections which may exist for this listener.
 *
 * @param attr
 *        The attribute set to query or modify.
 * @param backlog
 *        The number of outstanding connections to allow.
 *
 * @retval GLOBUS_SUCCESS
 *         The attribute was successfully updated.
 * @retval GLOBUS_GASS_TRANSFER_ERROR_NULL_POINTER
 *         The @a attr was GLOBUS_NULL.
 */
int
globus_gass_transfer_listenerattr_set_backlog(
    globus_gass_transfer_listenerattr_t *	attr,
    int						backlog)
{
    globus_object_t *				obj;
    globus_gass_object_type_listenerattr_instance_t *
						instance;

    obj = globus_object_upcast(*attr,
			       GLOBUS_GASS_OBJECT_TYPE_LISTENERATTR);
    if(obj == GLOBUS_NULL)
    {
	return GLOBUS_GASS_TRANSFER_ERROR_NULL_POINTER;
    }
    instance = globus_object_get_local_instance_data(obj);
    if(instance == GLOBUS_NULL)
    {
	return GLOBUS_GASS_TRANSFER_ERROR_NULL_POINTER;
    }
    instance->backlog = backlog;

    return GLOBUS_SUCCESS;
}
/* globus_gass_transfer_listenerattr_set_backlog() */

int
globus_gass_transfer_listenerattr_get_backlog(
    globus_gass_transfer_listenerattr_t *	attr,
    int	*					backlog)
{
    globus_object_t *				obj;
    globus_gass_object_type_listenerattr_instance_t *
						instance;

    obj = globus_object_upcast(*attr,
			       GLOBUS_GASS_OBJECT_TYPE_LISTENERATTR);
    if(obj == GLOBUS_NULL)
    {
	return GLOBUS_GASS_TRANSFER_ERROR_NULL_POINTER;
    }
    instance = globus_object_get_local_instance_data(obj);
    if(instance == GLOBUS_NULL)
    {
	return GLOBUS_GASS_TRANSFER_ERROR_NULL_POINTER;
    }
    if(backlog == GLOBUS_NULL)
    {
	return GLOBUS_GASS_TRANSFER_ERROR_NULL_POINTER;
    }
    *backlog = instance->backlog;

    return GLOBUS_SUCCESS;
}
/* globus_gass_transfer_listenerattr_get_backlog() */
/* @} */

/**
 * @name Listener Port
 */
/* @{ */
/**
 * Set/Get the port attribute for a GASS transfer attribute set.
 * @ingroup globus_gass_transfer_listenerattr
 *
 * This attribute allows the user to set the port to be used by
 * a GASS Transfer listener.
 *
 * @param attr
 *        The attribute set to query or modify.
 * @param port
 *        The TCP or UDP port number to use.
 *
 * @retval GLOBUS_SUCCESS
 *         The attribute was successfully updated.
 * @retval GLOBUS_GASS_TRANSFER_ERROR_NULL_POINTER
 *         The @a attr was GLOBUS_NULL.
 */
int
globus_gass_transfer_listenerattr_set_port(
    globus_gass_transfer_listenerattr_t *	attr,
    unsigned short				port)
{
    globus_object_t *				obj;
    globus_gass_object_type_listenerattr_instance_t *
						instance;

    obj = globus_object_upcast(*attr,
			       GLOBUS_GASS_OBJECT_TYPE_LISTENERATTR);
    if(obj == GLOBUS_NULL)
    {
	return GLOBUS_GASS_TRANSFER_ERROR_NULL_POINTER;
    }
    instance = globus_object_get_local_instance_data(obj);
    if(instance == GLOBUS_NULL)
    {
	return GLOBUS_GASS_TRANSFER_ERROR_NULL_POINTER;
    }
    instance->port = port;

    return GLOBUS_SUCCESS;
}
/* globus_gass_transfer_listenerattr_set_port() */

int
globus_gass_transfer_listenerattr_get_port(
    globus_gass_transfer_listenerattr_t *	attr,
    unsigned short *				port)
{
    globus_object_t *				obj;
    globus_gass_object_type_listenerattr_instance_t *
						instance;

    obj = globus_object_upcast(*attr,
			       GLOBUS_GASS_OBJECT_TYPE_LISTENERATTR);
    if(obj == GLOBUS_NULL)
    {
	return GLOBUS_GASS_TRANSFER_ERROR_NULL_POINTER;
    }
    instance = globus_object_get_local_instance_data(obj);
    if(instance == GLOBUS_NULL)
    {
	return GLOBUS_GASS_TRANSFER_ERROR_NULL_POINTER;
    }
    if(port == GLOBUS_NULL)
    {
	return GLOBUS_GASS_TRANSFER_ERROR_NULL_POINTER;
    }
    *port = instance->port;

    return GLOBUS_SUCCESS;
}
/* globus_gass_transfer_listenerattr_get_port() */
/* @} */


/* Module-Specific Functions */
#ifndef GLOBUS_DONT_DOCUMENT_INTERNAL
static
void
globus_l_gass_requestattr_copy(
   void *				src_data,
   void **				dst_data)
{
    globus_gass_object_type_requestattr_instance_t *
					src;
    globus_gass_object_type_requestattr_instance_t *
					dst;

    if(src_data == GLOBUS_NULL ||
       dst_data == GLOBUS_NULL)
    {
	return;
    }
    dst = globus_malloc(
	sizeof(globus_gass_object_type_requestattr_instance_t));
    src = (globus_gass_object_type_requestattr_instance_t *)
	src_data;

    if(dst == GLOBUS_NULL)
    {
	return;
    }
    memcpy(dst,
	   src_data,
	   sizeof(globus_gass_object_type_requestattr_instance_t));
    if(src->proxy_url)
    {
	dst->proxy_url = globus_libc_strdup(src->proxy_url);
    }
    *((globus_gass_object_type_requestattr_instance_t **)
	dst_data) = dst;
    return;
}
/* globus_l_gass_requestattr_copy() */

static
void
globus_l_gass_requestattr_destroy(
    void *				data)
{
    globus_gass_object_type_requestattr_instance_t *
					dst;
    dst = (globus_gass_object_type_requestattr_instance_t *) data;

    if(dst->proxy_url)
    {
	globus_free(dst->proxy_url);
    }
    globus_free(dst);
}
/* globus_l_gass_requestattr_destroy() */

static
void
globus_l_gass_socket_requestattr_copy(
    void *				src_data,
    void **				dst_data)
{
    globus_gass_object_type_socket_requestattr_instance_t *
					dst;

    if(src_data == GLOBUS_NULL ||
       dst_data == GLOBUS_NULL)
    {
	return;
    }
    dst = globus_malloc(
	sizeof(globus_gass_object_type_socket_requestattr_instance_t));
    if(dst == GLOBUS_NULL)
    {
	return;
    }
    memcpy(dst,
	   src_data,
	   sizeof(globus_gass_object_type_socket_requestattr_instance_t));
    *((globus_gass_object_type_socket_requestattr_instance_t **)
	dst_data) = dst;

    return;
}
/* globus_l_gass_socket_requestattr_copy() */

static
void
globus_l_gass_socket_requestattr_destroy(
    void *				data)
{
    globus_free(data);
}
/* globus_l_gass_socket_requestattr_destroy() */

static
void
globus_l_gass_secure_requestattr_copy(
    void *				src_data,
    void **				dst_data)
{
    globus_gass_object_type_secure_requestattr_instance_t *
					src;
    globus_gass_object_type_secure_requestattr_instance_t *
					dst;

    if(src_data == GLOBUS_NULL ||
       dst_data == GLOBUS_NULL)
    {
	return;
    }
    dst = globus_malloc(
	sizeof(globus_gass_object_type_secure_requestattr_instance_t));
    src = (globus_gass_object_type_secure_requestattr_instance_t *)
	src_data;
    if(dst == GLOBUS_NULL)
    {
	return;
    }
    memcpy(dst,
	   src_data,
	   sizeof(globus_gass_object_type_secure_requestattr_instance_t));
    if(src->subject)
    {
	dst->subject = globus_libc_strdup(src->subject);
    }
    *((globus_gass_object_type_secure_requestattr_instance_t **)
	dst_data) = dst;

    return;
}
/* globus_l_gass_secure_requestattr_copy() */

static
void
globus_l_gass_secure_requestattr_destroy(
    void *				data)
{
    globus_gass_object_type_secure_requestattr_instance_t *
					dst;
    dst = (globus_gass_object_type_secure_requestattr_instance_t *) data;

    if(dst->subject)
    {
	globus_free(dst->subject);
    }
    globus_free(dst);
}
/* globus_l_gass_secure_requestattr_destroy() */

static
void
globus_l_gass_listenerattr_copy(
    void *				src_data,
    void **				dst_data)
{
    globus_gass_object_type_listenerattr_instance_t *
					dst;

    if(src_data == GLOBUS_NULL ||
       dst_data == GLOBUS_NULL)
    {
	return;
    }
    dst = globus_malloc(
	sizeof(globus_gass_object_type_listenerattr_instance_t));
    if(dst == GLOBUS_NULL)
    {
	return;
    }
    memcpy(dst,
	   src_data,
	   sizeof(globus_gass_object_type_listenerattr_instance_t));
    *((globus_gass_object_type_listenerattr_instance_t **)
	dst_data) = dst;

    return;
}
/* globus_l_gass_listenerattr_copy() */

static
void
globus_l_gass_listenerattr_destroy(
    void *				data)
{
    globus_free(data);
}
/* globus_l_gass_listenerattr_destroy() */
#endif /* !GLOBUS_DONT_DOCUMENT_INTERNAL */
