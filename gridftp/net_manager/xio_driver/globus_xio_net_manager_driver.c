/*
 * Copyright 1999-2014 University of Chicago
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
#include "globus_xio_load.h"
#include "globus_common.h"
#include "globus_xio_net_manager_driver.h"

#include "globus_net_manager.h"
#include "globus_net_manager_attr.h"
#include "globus_net_manager_context.h"

GlobusDebugDeclare(GLOBUS_XIO_NET_MANAGER);
GlobusDebugDefine(GLOBUS_XIO_NET_MANAGER);
GlobusXIODeclareDriver(net_manager);

#define GlobusXIONetManagerDebugPrintf(level, message)                  \
    GlobusDebugPrintf(GLOBUS_XIO_NET_MANAGER, level, message)

#define GlobusXIONetManagerDebugEnter()                                 \
    GlobusXIONetManagerDebugPrintf(                                     \
        GLOBUS_XIO_NET_MANAGER_DEBUG_TRACE,                             \
        ("[%s] Entering\n", __func__))

#define GlobusXIONetManagerDebugExit()                                  \
    GlobusXIONetManagerDebugPrintf(                                     \
        GLOBUS_XIO_NET_MANAGER_DEBUG_TRACE,                             \
        ("[%s] Exiting\n", __func__))

typedef enum
{
    GLOBUS_XIO_NET_MANAGER_DEBUG_ERROR = 1,
    GLOBUS_XIO_NET_MANAGER_DEBUG_WARNING = 2,
    GLOBUS_XIO_NET_MANAGER_DEBUG_TRACE = 4,
    GLOBUS_XIO_NET_MANAGER_DEBUG_INFO = 8,
}
globus_xio_net_manager_debug_levels_t;

typedef struct
{
    /** net manager attributes, scoped by manager implementation */
    globus_net_manager_attr_t          *attr_array;

    /** task id associated with this op */
    char                               *task_id;

    /** net manager context created from the attr_array values */
    globus_net_manager_context_t        context;
}
globus_l_xio_net_manager_attr_t;

typedef struct
{
    globus_l_xio_net_manager_attr_t    *attr;
    const char                         *transport_name;
    globus_xio_driver_t                 transport_driver;
    char                               *local_contact;
}
globus_l_xio_net_manager_server_t;

typedef struct
{
    globus_l_xio_net_manager_attr_t    *attr;
    const char                         *transport_name;
    globus_xio_driver_t                 transport_driver;
    char                               *local_contact;
    char                               *remote_contact;
}
globus_l_xio_net_manager_link_t;

typedef struct
{
    globus_l_xio_net_manager_attr_t    *attr;
    const char                         *transport_name;
    globus_xio_driver_t                 transport_driver;
    globus_bool_t                       passive;
    char                               *local_contact;
    char                               *remote_contact;
}
globus_l_xio_net_manager_handle_t;

static
int
globus_l_xio_net_manager_activate(void);

static
int
globus_l_xio_net_manager_deactivate(void);

#include "version.h"

GlobusXIODeclareModule(net_manager);

GlobusXIODefineModule(net_manager) =
{
    "globus_xio_net_manager",
    globus_l_xio_net_manager_activate,
    globus_l_xio_net_manager_deactivate,
    NULL,
    NULL,
    &local_version
};

static
globus_result_t
globus_l_xio_net_manager_attr_init(
    void                              **attr)
{
    globus_result_t                     result = GLOBUS_SUCCESS;
    globus_l_xio_net_manager_attr_t    *a;

    a = malloc(sizeof(globus_l_xio_net_manager_attr_t));
    if (!a)
    {
        result = GlobusNetManagerErrorMemory("attr");
        goto malloc_attr_exit;
    }

    a->attr_array = NULL;
    a->task_id = NULL;
    a->context = NULL;

malloc_attr_exit:
    *attr = a;
    return result;
}
/* globus_l_xio_net_manager_attr_init() */

static
globus_result_t
globus_l_xio_net_manager_attr_copy(
    void                              **dest,
    void                               *src)
{
    globus_result_t                     result = GLOBUS_SUCCESS;
    globus_l_xio_net_manager_attr_t    *s = src, *d = NULL;

    if (!dest)
    {
        result = GlobusNetManagerErrorParameter("NULL dest");
        goto null_dest;
    }
    if (!s)
    {
        result = GlobusNetManagerErrorParameter("NULL src");
        goto null_src;
    }
    result = globus_l_xio_net_manager_attr_init((void **) &d);
    if (result)
    {
        goto malloc_d_failed;
    }
    if (s->task_id)
    {
        d->task_id = strdup(s->task_id);
        if (!d->task_id)
        {
            result = GlobusNetManagerErrorMemory("task-id");
            goto strdup_task_id_failed;
        }
    }

    if (s->attr_array)
    {
        result = globus_net_manager_attr_array_copy(
                &d->attr_array,
                s->attr_array);
        if (result)
        {
            goto attr_array_copy_failed;
        }
    }
    else
    {
        d->attr_array = NULL;
    }
    result = globus_net_manager_context_init(
            &d->context,
            d->attr_array);

    if (result)
    {
        globus_net_manager_attr_array_delete(d->attr_array);
attr_array_copy_failed:
        free(d->task_id);
strdup_task_id_failed:
        free(d);
        d = NULL;

        if (result == GLOBUS_SUCCESS)
        {
            result = GLOBUS_FAILURE;
        }
    }
malloc_d_failed:
null_src:
    *dest = d;
null_dest:
    return result;
}
/* globus_l_xio_net_manager_attr_copy() */

static
globus_result_t
globus_l_xio_net_manager_attr_set_string_options(
    globus_l_xio_net_manager_attr_t    *attr,
    const char                         *options_string)
{
    globus_result_t                     result = GLOBUS_SUCCESS;
    globus_list_t                      *options = NULL;
    globus_list_t                      *rev_options;
    int                                 num_options;
    globus_net_manager_attr_t          *new_attrs;
    globus_net_manager_context_t        new_context = NULL;
    char                               *scope = NULL;
    char                               *new_task_id = NULL;
    size_t                              attrnum = 0;

    rev_options = globus_list_from_string(options_string, ';', NULL);
    /* dislike that this func produces a reversed list */
    while (!globus_list_empty(rev_options))
    {
        globus_list_insert(
            &options, globus_list_remove(&rev_options, rev_options));
    }

    num_options = globus_list_size(options);

    if (num_options == 0)
    {
        goto no_options;
    }
    new_attrs = calloc(num_options+1, sizeof(globus_net_manager_attr_t));
    if (!new_attrs)
    {
        result = GlobusNetManagerErrorMemory("attr_array");
        goto new_attrs_calloc_fail;
    }
    while (!globus_list_empty(options))
    {
        char                           *opt, *val;

        opt = globus_list_remove(&options, options);
        if (*opt == '\0')
        {
            free(opt);
            continue;
        }
        val = strchr(opt, '=');
        if (!val)
        {
            result = GlobusNetManagerErrorParameter("Invalid option string.");
            free(opt);
            goto no_equals;
        }
        *val++ = '\0';

        if (strcmp(opt, "manager") == 0)
        {
            result = globus_net_manager_attr_init(
                    &new_attrs[attrnum++],
                    "net_manager",
                    opt,
                    val);
            if (result)
            {
                free(opt);
                new_attrs[attrnum-1] = globus_net_manager_null_attr;
                goto new_attr_init_fail;
            }
            free(scope);
            scope = strdup(val);
            if (!scope)
            {
                result = GlobusNetManagerErrorMemory("scope");
                free(opt);
                new_attrs[attrnum++] = globus_net_manager_null_attr;
                goto strdup_scope_fail;
            }
        }
        else if (strcmp(opt, "task-id") == 0)
        {
            free(new_task_id);
            new_task_id = strdup(val);
            if (!new_task_id)
            {
                result = GlobusNetManagerErrorMemory("task-id");
                free(opt);
                new_attrs[attrnum++] = globus_net_manager_null_attr;
                goto strdup_task_id_fail;
            }
        }
        else
        {
            result = globus_net_manager_attr_init(
                    &new_attrs[attrnum++],
                    scope ? scope : "global",
                    opt,
                    val);
            if (result)
            {
                free(opt);
                new_attrs[attrnum-1] = globus_net_manager_null_attr;
                goto new_attr_init_fail;
            }
        }
        free(opt);
    }
    new_attrs[attrnum++] = globus_net_manager_null_attr;
    if (new_attrs)
    {
        result = globus_net_manager_context_init(
            &new_context,
            new_attrs);
        if (result)
        {
            goto new_context_fail;
        }
        globus_net_manager_context_destroy(attr->context);
        attr->context = new_context;
    }

    if (new_task_id)
    {
        free(attr->task_id);
        attr->task_id = new_task_id;
        new_task_id = NULL;
    }
    if (new_attrs)
    {
        globus_net_manager_attr_array_delete(attr->attr_array);
        attr->attr_array = new_attrs;
        new_attrs = NULL;
    }

new_context_fail:
new_attr_init_fail:
strdup_task_id_fail:
strdup_scope_fail:
no_equals:
    free(new_task_id);
    free(scope);
    globus_net_manager_attr_array_delete(new_attrs);
new_attrs_calloc_fail:
    globus_list_destroy_all(options, free);
no_options:
    return result;
}
/* globus_l_xio_net_manager_attr_set_string_options() */

static
globus_result_t
globus_l_xio_net_manager_attr_get_string_options(
    globus_l_xio_net_manager_attr_t    *attr,
    char                              **out_string)
{
    size_t                              out_len = 0;
    char                               *output = NULL;
    globus_result_t                     result = GLOBUS_SUCCESS;

    if (attr->task_id)
    {
        out_len += snprintf(NULL, 0, "task-id=%s;", attr->task_id);
    }
    if (attr->attr_array)
    {
        for (int i = 0; attr->attr_array[i].scope != NULL; i++)
        {   
            out_len += snprintf(NULL, 0, "%s=%s;",
                        attr->attr_array[i].name,
                        attr->attr_array[i].value);
        }
    }
    output = malloc(out_len+1);
    if (!output)
    {
        result = GlobusNetManagerErrorMemory("string_options");
        goto output_malloc_fail;
    }
    out_len = 0;
    if (attr->task_id)
    {
        out_len += sprintf(output + out_len, "task-id=%s;", attr->task_id);
    }
    if (attr->attr_array)
    {
        for (int i = 0; attr->attr_array[i].scope != NULL; i++)
        {
            out_len += sprintf(output + out_len, "%s=%s;",
                        attr->attr_array[i].name,
                        attr->attr_array[i].value);
        }
    }
output_malloc_fail:
    *out_string = output;
    return result;
}
/* globus_l_xio_net_manager_attr_get_string_options() */

static
globus_result_t
globus_l_xio_net_manager_set_task_id(
    globus_l_xio_net_manager_attr_t    *attr,
    const char                         *in_string)
{
    globus_result_t                     result = GLOBUS_SUCCESS;
    char                               *new_task_id = NULL;

    if (in_string)
    {
        new_task_id = strdup(in_string);
        if (!new_task_id)
        {
            result = GlobusNetManagerErrorMemory("task_id");
            goto strdup_task_id_fail;
        }
    }
    free(attr->task_id);
    attr->task_id = new_task_id;

strdup_task_id_fail:
    return result;
}
/* globus_l_xio_net_manager_set_task_id() */

static
globus_result_t
globus_l_xio_net_manager_get_task_id(
    globus_l_xio_net_manager_attr_t    *attr,
    char                              **out_string)
{
    globus_result_t                     result = GLOBUS_SUCCESS;

    if (!out_string)
    {
        result = GlobusNetManagerErrorParameter("NULL out_string");
        goto null_out_string;
    }
    if (attr->task_id)
    {
        *out_string = strdup(attr->task_id);
        if (!*out_string)
        {
            result = GlobusNetManagerErrorMemory("task_id");
            goto strdup_task_id_fail;
        }
    }
    else
    {
        *out_string = NULL;
    }

strdup_task_id_fail:
null_out_string:
    return result;
}
/* globus_l_xio_net_manager_set_task_id() */

static
globus_result_t
globus_l_xio_net_manager_attr_cntl(
    void                               *attr,
    int                                 cmd,
    va_list                             ap)
{
    globus_result_t                     result = GLOBUS_SUCCESS;
    const char                         *const_in_string;
    char                              **out_string;
    const char                        **const_out_string;

    if (!attr)
    {
        result = GlobusNetManagerErrorParameter("NULL attr");
        goto null_attr;
    }
    
    switch (cmd)
    {
        case GLOBUS_XIO_SET_STRING_OPTIONS:
            const_in_string = va_arg(ap, const char *);
            if (const_in_string)
            {
                result = globus_l_xio_net_manager_attr_set_string_options(
                        attr, const_in_string);
            }
            break;
        case GLOBUS_XIO_GET_STRING_OPTIONS:
            out_string = va_arg(ap, char **);
            if (!out_string)
            {
                result = GlobusNetManagerErrorParameter("NULL out_string");
                break;
            }
            result = globus_l_xio_net_manager_attr_get_string_options(
                    attr, out_string);
            break;

        case GLOBUS_XIO_GET_DRIVER_NAME:
            const_out_string = va_arg(ap, const char **);
            if (!const_out_string)
            {
                result = GlobusNetManagerErrorParameter("NULL out_string");
                goto fail_get_driver_name;
            }
            *const_out_string = "net_manager";
            break;

        case GLOBUS_XIO_NET_MANAGER_SET_TASK_ID:
            const_in_string = va_arg(ap, char *);
            result = globus_l_xio_net_manager_set_task_id(
                    attr, const_in_string);
            break;
        case GLOBUS_XIO_NET_MANAGER_GET_TASK_ID:
            out_string = va_arg(ap, char **);
            result = globus_l_xio_net_manager_get_task_id(attr, out_string);
            break;
        default:
            result = GlobusNetManagerErrorParameter("Invalid command.");
    }

fail_get_driver_name:
null_attr:
    return result;
}
/* globus_l_xio_net_manager_attr_cntl() */

static
globus_result_t
globus_l_xio_net_manager_attr_destroy(
    void                               *attr)
{
    globus_result_t                     result = GLOBUS_SUCCESS;
    globus_l_xio_net_manager_attr_t    *a = attr;

    if (!a)
    {
        result = GlobusNetManagerErrorParameter("NULL attr.");
        goto null_a_exit;
    }
    globus_net_manager_context_destroy(a->context);
    globus_net_manager_attr_array_delete(a->attr_array);
    free(a->task_id);
    free(a);

null_a_exit:
    return result;
}

static
globus_result_t
globus_l_xio_net_manager_attr_array_to_string(
    const globus_net_manager_attr_t    *attrs,
    const char                         *transport_name,
    char                              **string_options)
{
    globus_result_t                     result = GLOBUS_SUCCESS;
    int                                 string_options_count = 0;
    size_t                              string_options_length = 1;
    char                               *p = NULL;

    if (!attrs)
    {
        *string_options=NULL;
        return result;
    }
    for (int i = 0; attrs[i].scope; i++)
    {
        if (strcmp(attrs[i].scope, transport_name) == 0)
        {
            string_options_count++;
            string_options_length +=
                strlen(attrs[i].name) + strlen(attrs[i].value) + 2;
        }
    }
    if (string_options_count)
    {
        int                             offset = 0;

        p = malloc(string_options_length);
        if (!p)
        {
            result = GlobusNetManagerErrorMemory("string_options");
            goto malloc_failed;
        }
        for (int i = 0; attrs[i].scope; i++)
        {
            if (strcmp(attrs[i].scope, transport_name) == 0)
            {
                offset += sprintf(p+offset, "%s=%s;",
                                    attrs[i].name, attrs[i].value);
            }
        }
    }
malloc_failed:
    *string_options = p;
    return result;
}
/* globus_l_xio_net_manager_attr_array_to_string() */

/**
 * @brief Apply an attribute array to a transport attribute
 */
static
globus_result_t
globus_l_xio_net_manager_transport_attr_apply(
    globus_xio_operation_t              op,
    const globus_net_manager_attr_t    *attrs)
{
    globus_result_t                     result = GLOBUS_SUCCESS;
    globus_xio_driver_t                 transport_driver;
    const char                         *transport_name;
    char                               *string_options = NULL;

    transport_driver = globus_xio_operation_get_transport_user_driver(op);

    result = globus_xio_driver_attr_cntl(
        op, transport_driver, GLOBUS_XIO_GET_DRIVER_NAME, &transport_name);
    if (result)
    {
        goto get_transport_name_failed;
    }
    result = globus_l_xio_net_manager_attr_array_to_string(
            attrs,
            transport_name,
            &string_options);
    if (result)
    {
        goto get_string_options_failed;
    }

    result = globus_xio_driver_attr_cntl(
        op, transport_driver, GLOBUS_XIO_SET_STRING_OPTIONS, string_options);

    free(string_options);
get_string_options_failed:
get_transport_name_failed:
    return result;
}
/* globus_l_xio_net_manager_transport_attr_apply() */

static
globus_result_t
globus_l_xio_net_manager_transport_handle_apply(
    globus_l_xio_net_manager_handle_t  *handle,
    globus_xio_operation_t              op,
    globus_net_manager_attr_t           *attrs)
{
    globus_result_t                     result = GLOBUS_SUCCESS;
    char                               *string_options = NULL;

    result = globus_l_xio_net_manager_attr_array_to_string(
            attrs,
            handle->transport_name,
            &string_options);
    if (result)
    {
        goto get_string_options_failed;
    }

    result = globus_xio_driver_handle_cntl(
            globus_xio_operation_get_driver_self_handle(op),
            handle->transport_driver,
            GLOBUS_XIO_SET_STRING_OPTIONS,
            string_options);

    free(string_options);
get_string_options_failed:
    return result;
}

static
globus_result_t
globus_l_xio_net_manager_get_handle_attr_array(
    globus_xio_operation_t              op,
    globus_xio_driver_t                 transport_driver,
    const char                         *transport_name,
    globus_net_manager_attr_t         **attr_array)
{
    globus_xio_driver_handle_t          driver_handle = NULL;
    globus_result_t                     result = GLOBUS_SUCCESS;
    char                               *string_opts = NULL;

    driver_handle = globus_xio_operation_get_driver_handle(op);

    result = globus_xio_driver_handle_cntl(
        driver_handle,
        transport_driver,
        GLOBUS_XIO_GET_STRING_OPTIONS,
        &string_opts);
    if (result)
    {
        goto get_string_opts_fail;
    }

    result = globus_net_manager_attr_array_from_string(
        attr_array,
        transport_name,
        string_opts);
    free(string_opts);

get_string_opts_fail:

    return result;
}
/* globus_l_xio_net_manager_get_handle_attr_array() */

static
globus_result_t
globus_l_xio_net_manager_get_attr_array(
    globus_xio_operation_t              op,
    globus_xio_driver_t                 transport_driver,
    const char                         *transport_name,
    globus_net_manager_attr_t         **attr_array)
{
    globus_result_t                     result = GLOBUS_SUCCESS;
    char                               *string_opts = NULL;

    result = globus_xio_driver_attr_cntl(
        op,
        transport_driver,
        GLOBUS_XIO_GET_STRING_OPTIONS,
        &string_opts);
    if (result)
    {
        goto get_string_opts_fail;
    }

    result = globus_net_manager_attr_array_from_string(
        attr_array,
        transport_name,
        string_opts);
    free(string_opts);

get_string_opts_fail:

    return result;
}
/* globus_l_xio_net_manager_get_attr_array() */

static
globus_result_t
globus_l_xio_net_manager_server_pre_init(
    void *                              driver_attr,
    const globus_xio_contact_t         *contact_info,
    globus_xio_operation_t              op)
{
    globus_result_t                     result = GLOBUS_SUCCESS;
    globus_l_xio_net_manager_attr_t    *attr = driver_attr;
    globus_xio_driver_t                 transport_driver = NULL;
    const char                         *transport_name = NULL;
    globus_net_manager_attr_t          *transport_attrs = NULL,
                                       *new_attrs = NULL;

    if (!attr)
    {
        goto no_attr;
    }
    transport_driver = globus_xio_operation_get_transport_user_driver(
            op);

    result = globus_xio_driver_attr_cntl(
            op,
            transport_driver,
            GLOBUS_XIO_GET_DRIVER_NAME,
            &transport_name);
    if (result)
    {
        goto get_driver_name_fail;
    }
    
    result = globus_l_xio_net_manager_get_attr_array(
            op,
            transport_driver,
            transport_name,
            &transport_attrs);

    if (result)
    {
        goto get_array_fail;
    }
    result = globus_net_manager_context_pre_listen(
        attr->context,
        attr->task_id ? attr->task_id : "unset",
        transport_name,
        transport_attrs,
        &new_attrs);
    if (result)
    {
        goto pre_listen_fail;
    }

    if (new_attrs)
    {
        result = globus_l_xio_net_manager_transport_attr_apply(op, new_attrs);
    }
    globus_net_manager_attr_array_delete(new_attrs);
pre_listen_fail:
    globus_net_manager_attr_array_delete(transport_attrs);
get_array_fail:
get_driver_name_fail:
no_attr:
    return result;
}
/* globus_l_xio_net_manager_server_pre_init() */

static
globus_result_t
globus_l_xio_net_manager_server_init(
    void *                              driver_attr,
    const globus_xio_contact_t         *contact_info,
    globus_xio_operation_t              op)
{
    globus_result_t                     result = GLOBUS_SUCCESS;
    globus_net_manager_attr_t          *transport_attrs = NULL,
                                       *new_attrs = NULL;
    globus_l_xio_net_manager_server_t  *server = NULL;
    char                               *new_contact_string = NULL;
    globus_xio_contact_t                new_contact_info = {NULL};

    if (!driver_attr)
    {
        result = globus_xio_driver_pass_server_init(
            op,
            contact_info,
            NULL);

        goto no_attr;
    }
    server = malloc(sizeof(globus_l_xio_net_manager_server_t));
    if (!server)
    {
        result = GlobusNetManagerErrorMemory("server");
        goto server_malloc_fail;
    }
    server->transport_driver = globus_xio_operation_get_transport_user_driver(
            op);

    result = globus_xio_driver_attr_cntl(
            op,
            server->transport_driver,
            GLOBUS_XIO_GET_DRIVER_NAME,
            &server->transport_name);
    if (result)
    {
        goto get_driver_name_fail;
    }
    result = globus_l_xio_net_manager_attr_copy(
            (void **)&server->attr, driver_attr);
    if (result)
    {
        goto copy_attr_fail;
    }
    
    result = globus_l_xio_net_manager_get_attr_array(
            op,
            server->transport_driver,
            server->transport_name,
            &transport_attrs);
    if (result)
    {
        goto get_attr_array_fail;
    }

    result = globus_net_manager_context_post_listen(
        server->attr->context,
        server->attr->task_id ? server->attr->task_id : "unset",
        server->transport_name,
        contact_info->unparsed,
        transport_attrs,
        &new_contact_string,
        &new_attrs);
    if (result)
    {
        goto post_listen_fail;
    }

    if (new_contact_string)
    {
        server->local_contact = new_contact_string;
        new_contact_string = NULL;
        result = globus_xio_contact_parse(
                &new_contact_info,
                new_contact_string);
        if (result)
        {
            goto parse_contact_fail;
        }
    }
    else
    {
        server->local_contact = strdup(contact_info->unparsed);
        if (server->local_contact == NULL)
        {
            result = GlobusNetManagerErrorMemory("local_contact");
            goto strdup_contact_fail;
        }
    }
    if (new_attrs)
    {
        result = globus_l_xio_net_manager_transport_attr_apply(op, new_attrs);
        if (result)
        {
            goto apply_attr_fail;
        }
    }

    result = globus_xio_driver_pass_server_init(
        op,
        new_contact_info.unparsed ? &new_contact_info : contact_info,
        server);

apply_attr_fail:
    globus_xio_contact_destroy(&new_contact_info);
    if (result)
    {
        free(server->local_contact);
    }
strdup_contact_fail:
parse_contact_fail:
    free(new_contact_string);
    globus_net_manager_attr_array_delete(new_attrs);
post_listen_fail:
    globus_net_manager_attr_array_delete(transport_attrs);
get_attr_array_fail:
    if (result)
    {
        globus_l_xio_net_manager_attr_destroy(server->attr);
copy_attr_fail:
get_driver_name_fail:
        free(server);
    }
server_malloc_fail:
no_attr:
    return result;
}
/* globus_l_xio_net_manager_server_init() */

static
void
globus_l_xio_net_manager_server_accept_callback(
    globus_xio_operation_t              op,
    globus_result_t                     result,
    void                               *user_arg)
{
    globus_l_xio_net_manager_link_t    *link = user_arg;

    if (result && link)
    {
        globus_l_xio_net_manager_attr_destroy(link->attr);
        free(link->local_contact);
        free(link->remote_contact);
        free(link);
        link = NULL;
    }

    globus_xio_driver_finished_accept(op, link, result);
}
/* globus_l_xio_net_manager_server_accept_callback() */

static
globus_result_t
globus_l_xio_net_manager_server_accept(
    void *                              driver_server,
    globus_xio_operation_t              op)
{
    globus_result_t                     result = GLOBUS_SUCCESS;
    globus_l_xio_net_manager_server_t  *server = driver_server;
    globus_l_xio_net_manager_link_t    *link = NULL;
    globus_net_manager_attr_t          *new_attr_array = NULL;

    if (! server)
    {
        goto no_server;
    }
    result = globus_net_manager_context_pre_accept(
        server->attr->context,
        server->attr->task_id ? server->attr->task_id : "unset",
        server->transport_name,
        server->local_contact,
        server->attr->attr_array,
        &new_attr_array);

    if (result)
    {
        goto pre_accept_fail;
    }
    link = malloc(sizeof(globus_l_xio_net_manager_link_t));
    if (link == NULL)
    {
        result = GlobusNetManagerErrorMemory("link");
        goto link_malloc_fail;
    }
    link->local_contact = strdup(server->local_contact);
    if (link->local_contact == NULL)
    {
        result = GlobusNetManagerErrorMemory("local_contact");
        goto strdup_local_contact_fail;
    }
    link->remote_contact = NULL;
    if (server->attr)
    {
        result = globus_l_xio_net_manager_attr_copy(
                (void **) &link->attr,
                server->attr);
    }
    else
    {
        result = globus_l_xio_net_manager_attr_init(
                (void **) &link->attr);
    }

    if (result != GLOBUS_SUCCESS)
    {
        goto attr_copy_fail;
    }

    link->transport_name = server->transport_name;
    link->transport_driver = server->transport_driver;

    if (new_attr_array)
    {
        globus_net_manager_attr_array_delete(link->attr->attr_array);
        link->attr->attr_array = new_attr_array;
        new_attr_array = NULL;
    }

no_server:
    result = globus_xio_driver_pass_accept(
            op,
            globus_l_xio_net_manager_server_accept_callback, 
            link);

    if (result != GLOBUS_SUCCESS && link != NULL)
    {
        globus_l_xio_net_manager_attr_destroy(link->attr);
attr_copy_fail:
        free(link->local_contact);
strdup_local_contact_fail:
        free(link);
    }
link_malloc_fail:
pre_accept_fail:
    return result;
}
/* globus_l_xio_net_manager_server_accept() */

static
globus_result_t
globus_l_xio_net_manager_server_destroy(
    void *                              driver_server)
{
    globus_result_t                     result = GLOBUS_SUCCESS;
    globus_l_xio_net_manager_server_t  *server = driver_server;

    if (server)
    {        
        result = globus_net_manager_context_end_listen(
            server->attr->context,
            server->attr->task_id ? server->attr->task_id : "unset",
            server->transport_name,
            server->local_contact,
            server->attr->attr_array);
        if (result)
        {
            goto end_listen_fail;
        }

        globus_l_xio_net_manager_attr_destroy(server->attr);
        free(server->local_contact);
        free(server);
    }
    return result;
    
end_listen_fail:
    return result;
}

static
void
globus_l_xio_net_manager_connect_callback(
    globus_xio_operation_t              op,
    globus_result_t                     result,
    void *                              user_arg)
{
    globus_l_xio_net_manager_handle_t  *handle = user_arg;
    globus_net_manager_attr_t          *transport_opts = NULL,
                                       *new_transport_opts = NULL;
    globus_xio_driver_handle_t          driver_handle =
            globus_xio_operation_get_driver_handle(op);

    if (handle == NULL)
    {
        goto no_handle;
    }
    if (result)
    {
        goto failed_open;
    }

    /* Connect-side, call post-connect */
    result = globus_l_xio_net_manager_get_attr_array(
            op,
            handle->transport_driver,
            handle->transport_name,
            &transport_opts);
    if (result)
    {
        goto get_transport_opts_fail;
    }

    result = globus_xio_driver_handle_cntl(
            driver_handle,
            handle->transport_driver,
            GLOBUS_XIO_GET_LOCAL_NUMERIC_CONTACT,
            &handle->local_contact);
    if (result)
    {
        goto get_local_contact_fail;
    }

    result = globus_net_manager_context_post_connect(
            handle->attr->context,
            handle->attr->task_id ? handle->attr->task_id : "unset",
            handle->transport_name,
            handle->local_contact,
            handle->remote_contact,
            transport_opts,
            &new_transport_opts);
    if (result)
    {
        goto post_connect_fail;
    }
    if (new_transport_opts)
    {
        result = globus_l_xio_net_manager_transport_handle_apply(
                handle, op, new_transport_opts);
    }
    globus_net_manager_attr_array_delete(new_transport_opts);
    if (result)
    {
post_connect_fail:
        free(handle->remote_contact);
        free(handle->local_contact);
    }
get_local_contact_fail:
    globus_net_manager_attr_array_delete(transport_opts);
get_transport_opts_fail:
failed_open:
    if (result)
    {
        globus_l_xio_net_manager_attr_destroy(handle->attr);
        free(handle);
        handle = NULL;
    }
no_handle:
    globus_xio_driver_finished_open(handle, op, result);
}

static
globus_result_t
globus_l_xio_net_manager_connect(
    const globus_xio_contact_t *        contact_info,
    void *                              driver_attr,
    globus_xio_operation_t              op)
{
    globus_result_t                     result = GLOBUS_SUCCESS;
    globus_l_xio_net_manager_handle_t  *handle = NULL;
    char                               *contact_out = NULL;
    char                               *string_opts = NULL;
    globus_net_manager_attr_t          *attrs = NULL, *attr_array_out = NULL;
    globus_xio_contact_t                new_contact_info = {NULL};
    
    if (!driver_attr)
    {
        goto no_attr;
    }
    handle = malloc(sizeof(globus_l_xio_net_manager_handle_t));
    if (handle == NULL)
    {
        result = GlobusNetManagerErrorMemory("handle");
        goto malloc_handle_fail;
    }
    handle->local_contact = handle->remote_contact = NULL;

    result = globus_l_xio_net_manager_attr_copy(
            (void **)&handle->attr, driver_attr);
    if (result != GLOBUS_SUCCESS)
    {
        goto attr_copy_fail;
    }

    handle->passive = GLOBUS_FALSE;
    handle->transport_driver = globus_xio_operation_get_transport_user_driver(
            op);
    result = globus_xio_driver_attr_cntl(
            op,
            handle->transport_driver,
            GLOBUS_XIO_GET_DRIVER_NAME,
            &handle->transport_name);
    if (result)
    {
        goto get_driver_name_fail;
    }

    result = globus_xio_driver_attr_cntl(
        op,
        handle->transport_driver,
        GLOBUS_XIO_GET_STRING_OPTIONS,
        &string_opts);
    if (result)
    {
        goto get_string_opts_fail;
    }
    result = globus_net_manager_attr_array_from_string(
        &attrs,
        handle->transport_name,
        string_opts);
    if (result)
    {
        goto array_from_string_fail;
    }

    result = globus_net_manager_context_pre_connect(
            handle->attr->context,
            handle->attr->task_id ? handle->attr->task_id : "unset",
            handle->transport_name,
            contact_info->unparsed,
            attrs,
            &contact_out,
            &attr_array_out);

    if (result != GLOBUS_SUCCESS)
    {
        goto pre_connect_fail;
    }
    if (contact_out)
    {
        result = globus_xio_contact_parse(&new_contact_info, contact_out);
        if (result != GLOBUS_SUCCESS)
        {
            goto new_contact_parse_fail;
        }
        handle->remote_contact = contact_out;
        contact_out = NULL;
    }
    else
    {
        handle->remote_contact = strdup(contact_info->unparsed);
        if (handle->remote_contact == NULL)
        {
            result = GlobusNetManagerErrorMemory("remote_contact");
            goto strdup_remote_contact_fail;
        }
    }
    if (attr_array_out)
    {
        globus_net_manager_attr_array_delete(handle->attr->attr_array);
        handle->attr->attr_array = attr_array_out;

        result = globus_l_xio_net_manager_transport_attr_apply(op, attr_array_out);
        if (result != GLOBUS_SUCCESS)
        {
            goto attr_apply_fail;
        }
    }

no_attr:
    result = globus_xio_driver_pass_open(
        op,
        new_contact_info.unparsed ? &new_contact_info : contact_info,
        globus_l_xio_net_manager_connect_callback,
        handle);

attr_apply_fail:
    if (result && handle)
    {
        free(handle->remote_contact);
    }
strdup_remote_contact_fail:
new_contact_parse_fail:
    free(contact_out);
pre_connect_fail:
    globus_net_manager_attr_array_delete(attrs);
array_from_string_fail:
    free(string_opts);
get_string_opts_fail:
get_driver_name_fail:
    if (result && handle)
    {
        globus_l_xio_net_manager_attr_destroy(handle->attr);
attr_copy_fail:
        free(handle);
    }
malloc_handle_fail:
    return result;
}
/* globus_l_xio_net_manager_connect() */

static
void
globus_l_xio_net_manager_accept_callback(
    globus_xio_operation_t              op,
    globus_result_t                     result,
    void                               *callback_arg)
{
    globus_l_xio_net_manager_link_t    *link = callback_arg;
    globus_l_xio_net_manager_handle_t  *handle = NULL;
    char                               *string_opts = NULL;
    globus_net_manager_attr_t          *attrs = NULL, *attr_array_out = NULL;
    globus_xio_driver_handle_t          driver_handle =
                globus_xio_operation_get_driver_handle(op);

    if (result)
    {
        goto failed_accept;
    }
    if (link == NULL)
    {
        goto no_link;
    }
    handle = malloc(sizeof(globus_l_xio_net_manager_handle_t));
    if (handle == NULL)
    {
        result = GlobusNetManagerErrorMemory("handle");
        goto malloc_handle_fail;
    }

    handle->passive = GLOBUS_TRUE;
    handle->transport_driver = link->transport_driver;
    handle->transport_name = link->transport_name;

    handle->local_contact = link->local_contact;
    link->local_contact = NULL;

    result = globus_xio_driver_handle_cntl(
            driver_handle,
            handle->transport_driver,
            GLOBUS_XIO_GET_REMOTE_NUMERIC_CONTACT,
            &handle->remote_contact);
    if (result)
    {
        goto get_remote_contact_fail;
    }
    if (!handle->remote_contact)
    {
        result = GlobusNetManagerErrorInit(
            handle->transport_name, "Unable to get remote contact.");
        goto get_remote_contact_fail;
    }

    handle->attr = link->attr;
    link->attr = NULL;

    result = globus_xio_driver_attr_cntl(
        op,
        handle->transport_driver,
        GLOBUS_XIO_GET_STRING_OPTIONS,
        &string_opts);
    if (result)
    {
        goto get_string_opts_fail;
    }
    result = globus_net_manager_attr_array_from_string(
        &attrs,
        handle->transport_name,
        string_opts);
    if (result)
    {
        goto array_from_string_fail;
    }

    result = globus_net_manager_context_post_accept(
            handle->attr->context,
            handle->attr->task_id ? handle->attr->task_id : "unset",
            handle->transport_name,
            handle->local_contact,
            handle->remote_contact,
            attrs,
            &attr_array_out);

    if (result != GLOBUS_SUCCESS)
    {
        goto post_accept_fail;
    }
    if (attr_array_out)
    {
        globus_net_manager_attr_array_delete(handle->attr->attr_array);
        handle->attr->attr_array = attr_array_out;
        attr_array_out = NULL;

        result = globus_l_xio_net_manager_transport_handle_apply(
                handle, op, attr_array_out);
        if (result != GLOBUS_SUCCESS)
        {
            goto attr_apply_fail;
        }
    }

attr_apply_fail:
post_accept_fail:
    globus_net_manager_attr_array_delete(attr_array_out);
    globus_net_manager_attr_array_delete(attrs);
array_from_string_fail:
    free(string_opts);
    if (result)
    {
get_string_opts_fail:
        globus_l_xio_net_manager_attr_destroy(handle->attr);
        free(handle->remote_contact);
get_remote_contact_fail:
        free(handle->local_contact);
        free(handle);
        handle = NULL;
    }
malloc_handle_fail:
no_link:
failed_accept:
    globus_xio_driver_finished_open(handle, op, result);
}
/* globus_l_xio_net_manager_accept_callback() */

static
globus_result_t
globus_l_xio_net_manager_accept(
    const globus_xio_contact_t *        contact_info,
    void *                              driver_link,
    void *                              driver_attr,
    globus_xio_operation_t              op)
{
    globus_result_t                     result = GLOBUS_SUCCESS;
    globus_l_xio_net_manager_link_t    *link = driver_link;

    result = globus_xio_driver_pass_open(op, contact_info,
            globus_l_xio_net_manager_accept_callback,
            link);

    return result;
}
/* globus_l_xio_net_manager_accept() */

static
globus_result_t
globus_l_xio_net_manager_open(
    const globus_xio_contact_t *        contact_info,
    void *                              driver_link,
    void *                              driver_attr,
    globus_xio_operation_t              op)
{
    globus_result_t                     result = GLOBUS_SUCCESS;

    if (contact_info->unparsed != NULL)
    {
        result = globus_l_xio_net_manager_connect(
                contact_info,
                driver_attr,
                op);
    }
    else
    {
        result = globus_l_xio_net_manager_accept(
                contact_info,
                driver_link,
                driver_attr,
                op);
    }
    return result;
}


static
void
globus_l_xio_net_manager_close_cb(
    globus_xio_operation_t              op,
    globus_result_t                     result,
    void *                              user_arg)
{
    globus_l_xio_net_manager_handle_t  *handle = user_arg;

    if (!handle)
    {
        goto no_handle;
    }
    if (result == GLOBUS_SUCCESS)
    {
        result = globus_net_manager_context_post_close(
                handle->attr->context,
                handle->attr->task_id ? handle->attr->task_id : "unset",
                handle->transport_name,
                handle->local_contact,
                handle->remote_contact,
                handle->attr->attr_array);
    }
    globus_l_xio_net_manager_attr_destroy(handle->attr);
    free(handle->remote_contact);
    free(handle->local_contact);
    free(handle);
no_handle:
    globus_xio_driver_finished_close(op, result);
}

static
globus_result_t
globus_l_xio_net_manager_close(
    void                               *driver_specific_handle,
    void                               *attr,
    globus_xio_operation_t              op)
{
    globus_l_xio_net_manager_handle_t  *handle = driver_specific_handle;
    globus_result_t                     result = GLOBUS_SUCCESS;
    globus_net_manager_attr_t          *transport_opts = NULL;

    if (!handle)
    {
        goto no_handle;
    }
    result = globus_l_xio_net_manager_get_handle_attr_array(
            op,
            handle->transport_driver,
            handle->transport_name,
            &transport_opts);
    if (result)
    {
        goto get_attr_array_fail;
    }
    globus_net_manager_attr_array_delete(handle->attr->attr_array);
    handle->attr->attr_array = transport_opts;
    transport_opts = NULL;

    result = globus_net_manager_context_pre_close(
            handle->attr->context,
            handle->attr->task_id ? handle->attr->task_id : "unset",
            handle->transport_name,
            handle->local_contact,
            handle->remote_contact,
            handle->attr->attr_array);
    if (result)
    {
        goto pre_close_fail;
    }

no_handle:
    result = globus_xio_driver_pass_close(
        op, globus_l_xio_net_manager_close_cb, handle);

pre_close_fail:
    globus_net_manager_attr_array_delete(transport_opts);

get_attr_array_fail:
    return result;
}

static
globus_result_t
globus_l_xio_net_manager_link_destroy(
    void                               *driver_link)
{
    globus_l_xio_net_manager_link_t    *link = driver_link;

    if (link)
    {
        globus_l_xio_net_manager_attr_destroy(link->attr);
        free(link->local_contact);
        free(link->remote_contact);
        free(link);
    }
    return GLOBUS_SUCCESS;
}
/* globus_l_xio_net_manager_link_destroy() */


static
globus_result_t
globus_l_xio_net_manager_init(
    globus_xio_driver_t *               out_driver)
{
    globus_xio_driver_t                 driver;
    globus_result_t                     result = GLOBUS_SUCCESS;

    result = globus_xio_driver_init(&driver, "net_manager", NULL);
    if(result != GLOBUS_SUCCESS)
    {
        return result;
    }

    globus_xio_driver_set_transform(
        driver,
        globus_l_xio_net_manager_open,
        globus_l_xio_net_manager_close,
        NULL,
        NULL,
        NULL,
        NULL);

    globus_xio_driver_set_server(
        driver,
        globus_l_xio_net_manager_server_init,
        globus_l_xio_net_manager_server_accept,
        globus_l_xio_net_manager_server_destroy,
        NULL,
        NULL,
        globus_l_xio_net_manager_link_destroy);

    globus_xio_driver_set_server_pre_init(
        driver,
        globus_l_xio_net_manager_server_pre_init);

    globus_xio_driver_set_attr(
        driver,
        globus_l_xio_net_manager_attr_init,
        globus_l_xio_net_manager_attr_copy,
        globus_l_xio_net_manager_attr_cntl,
        globus_l_xio_net_manager_attr_destroy);

    *out_driver = driver;

    return GLOBUS_SUCCESS;
}

static
void
globus_l_xio_net_manager_destroy(
    globus_xio_driver_t                 driver)
{
    globus_xio_driver_destroy(driver);
}

GlobusXIODefineDriver(
    net_manager,
    globus_l_xio_net_manager_init,
    globus_l_xio_net_manager_destroy);

static
int
globus_l_xio_net_manager_activate(void)
{
    int                                 rc;

    GlobusDebugInit(GLOBUS_XIO_NET_MANAGER, TRACE);

    rc = globus_module_activate(GLOBUS_XIO_MODULE);
    if(rc != GLOBUS_SUCCESS)
    {
        goto activate_xio_fail;
    }
    rc = globus_module_activate(GLOBUS_NET_MANAGER_MODULE);

    if(rc == GLOBUS_SUCCESS)
    {
        GlobusXIORegisterDriver(net_manager);
    }
    else
    {
        globus_module_deactivate(GLOBUS_XIO_MODULE);
    }

activate_xio_fail:
    return rc;
}

static
int
globus_l_xio_net_manager_deactivate(void)
{
    GlobusXIOUnRegisterDriver(net_manager);
    globus_module_deactivate(GLOBUS_NET_MANAGER_MODULE);
    globus_module_deactivate(GLOBUS_XIO_MODULE);
    return 0;
}
