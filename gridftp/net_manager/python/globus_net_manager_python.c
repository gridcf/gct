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

#include "globus_common.h"
#include "globus_error_string.h"
#include "globus_net_manager.h"
#include "globus_net_manager_python.h"
#include "version.h"

#include <Python.h>

#if PY_VERSION_HEX >= 0x03000000
#define PyString_AsString   PyUnicode_AsUTF8
#define PyString_Check      PyUnicode_Check
#define PyString_FromString PyUnicode_FromString
#endif

typedef struct
{
    char                               *key;
    PyObject                           *module;
    PyObject                           *pre_listen;
    PyObject                           *post_listen;
    PyObject                           *end_listen;
    PyObject                           *pre_accept;
    PyObject                           *post_accept;
    PyObject                           *pre_connect;
    PyObject                           *post_connect;
    PyObject                           *pre_close;
    PyObject                           *post_close;
}
globus_l_python_modref_t;

static globus_hashtable_t               globus_l_python_modules;
static globus_mutex_t                   globus_l_python_modules_lock;

/**
 * @brief Resolve a python function name
 * @details
 * Check for a symbol named by funcname in the given module. If it is
 * a callable object, return it. Otherwise return NULL.
 *
 * @param module
 * @param funcname
 */
static
PyObject *
globus_l_python_resolve_func(
    PyObject                           *module,
    const char                         *funcname)
{
    PyObject                           *pyfunc = NULL;
    pyfunc = PyObject_GetAttrString(module, funcname);
    if (pyfunc && !PyCallable_Check(pyfunc))
    {
        Py_DECREF(pyfunc);
        pyfunc = NULL;
    }
    return pyfunc;
}

static
globus_result_t
globus_l_python_module(
    const globus_net_manager_attr_t    *attrs,
    globus_l_python_modref_t          **pymod)
{
    globus_result_t                     result = GLOBUS_SUCCESS;
    int                                 rc = 0;
    globus_l_python_modref_t           *modref = NULL;
    PyObject                           *pymodname = NULL;

    for (int i = 0; attrs != NULL && attrs[i].scope != NULL; i++)
    {
        if (strcmp(attrs[i].scope, "python") == 0)
        {
            if (strcmp(attrs[i].name, "pymod") == 0)
            {
                modref = globus_hashtable_lookup(
                    &globus_l_python_modules, attrs[i].value);
                if (!modref)
                {
                    modref = malloc(sizeof(globus_l_python_modref_t));
                    if (!modref)
                    {
                        result = GlobusNetManagerErrorMemory("modref");
                        goto modref_malloc_fail;
                    }
                    modref->key = strdup(attrs[i].value);
                    if (!modref->key)
                    {
                        result = GlobusNetManagerErrorMemory("key");
                        goto strdup_modref_key_fail;
                    }
                    pymodname = PyString_FromString(modref->key);
                    if (!pymodname)
                    {
                        result = GlobusNetManagerErrorMemory("pymodname");
                        goto modref_key_to_pystring_fail;
                    }
                    modref->module = PyImport_Import(pymodname);
                    if (!modref->module)
                    {
                        result = GlobusNetManagerErrorMemory("module");
                        goto module_import_fail;
                    }
                    modref->pre_listen = globus_l_python_resolve_func(
                            modref->module, "pre_listen");
                    modref->post_listen = globus_l_python_resolve_func(
                            modref->module, "post_listen"); 
                    modref->end_listen = globus_l_python_resolve_func(
                            modref->module, "end_listen"); 
                    modref->pre_accept = globus_l_python_resolve_func(
                            modref->module, "pre_accept"); 
                    modref->post_accept = globus_l_python_resolve_func(
                            modref->module, "post_accept"); 
                    modref->pre_connect = globus_l_python_resolve_func(
                            modref->module, "pre_connect"); 
                    modref->post_connect = globus_l_python_resolve_func(
                            modref->module, "post_connect"); 
                    modref->pre_close = globus_l_python_resolve_func(
                            modref->module, "pre_close"); 
                    modref->post_close = globus_l_python_resolve_func(
                            modref->module, "post_close"); 

                    rc = globus_hashtable_insert(
                            &globus_l_python_modules,
                            modref->key,
                            modref);
                    if (rc != GLOBUS_SUCCESS)
                    {
                        result = GlobusNetManagerErrorMemory(
                                "hashtable_insert");
                        goto hashtable_insert_fail;
                    }
                }
                Py_XDECREF(pymodname);
                break;
            }
        }
    }
hashtable_insert_fail:
    if (result != GLOBUS_SUCCESS)
    {
        Py_XDECREF(modref->pre_listen);
        Py_XDECREF(modref->post_listen);
        Py_XDECREF(modref->end_listen);
        Py_XDECREF(modref->pre_accept);
        Py_XDECREF(modref->post_accept);
        Py_XDECREF(modref->pre_connect);
        Py_XDECREF(modref->post_connect);
        Py_XDECREF(modref->pre_close);
        Py_XDECREF(modref->post_close);
        Py_XDECREF(modref->module);
    }
module_import_fail:
modref_key_to_pystring_fail:
    if (result != GLOBUS_SUCCESS)
    {
        free(modref->key);
strdup_modref_key_fail:
        free(modref);
        modref = NULL;
    }
modref_malloc_fail:
    *pymod = modref;
    return result;
}
/* globus_l_python_module() */

static
globus_result_t
globus_l_python_attr_array_to_pylist(
    const globus_net_manager_attr_t    *attr_array,
    PyObject                          **attr_list)
{
    globus_result_t                     result = GLOBUS_SUCCESS;
    ssize_t                             num_attrs = 0;
    PyObject                           *pylist = NULL;

    for (int i = 0; attr_array != NULL && attr_array[i].scope != NULL; i++)
    {
        num_attrs++;
    }
    pylist = PyList_New(num_attrs);
    if (!pylist)
    {
        result = GlobusNetManagerErrorMemory("pylist");
        goto pylist_new_fail;
    }

    for (int i = 0; attr_array != NULL && attr_array[i].scope != NULL; i++)
    {
        PyObject                       *tuple, *pyscope, *pyname, *pyvalue;

        tuple = PyTuple_New(3);
        if (!tuple)
        {
            result = GlobusNetManagerErrorMemory("tuple");
            goto pytuple_new_fail;
        }
        PyList_SetItem(pylist, i, tuple);

        pyscope = PyString_FromString(attr_array[i].scope);
        if (!pyscope)
        {
            result = GlobusNetManagerErrorMemory("scope");
            goto pyscope_new_fail;
        }
        PyTuple_SetItem(tuple, 0, pyscope);

        pyname = PyString_FromString(attr_array[i].name);
        if (!pyname)
        {
            result = GlobusNetManagerErrorMemory("pyname");
            goto pyname_new_fail;
        }
        PyTuple_SetItem(tuple, 1, pyname);

        pyvalue = PyString_FromString(attr_array[i].value);
        if (!pyvalue)
        {
            result = GlobusNetManagerErrorMemory("pyvalue");
            goto pyvalue_new_fail;
        }
        PyTuple_SetItem(tuple, 2, pyvalue);
    }
    if (result)
    {
pyvalue_new_fail:
pyname_new_fail:
pyscope_new_fail:
pytuple_new_fail:
        Py_DECREF(pylist);
        pylist = NULL;
    }
pylist_new_fail:
    *attr_list = pylist;
    return result;
}
/* globus_l_python_attr_array_to_pylist() */

static
globus_result_t
globus_l_python_pylist_to_attr_array(
    PyObject                           *pylist,
    globus_net_manager_attr_t          **attr_array_out)
{
    globus_result_t                     result = GLOBUS_SUCCESS;
    ssize_t                             list_size = 0, i = 0;
    globus_net_manager_attr_t          *attr_array = NULL;

    list_size = PyList_Size(pylist);
    attr_array = calloc(list_size+1, sizeof(globus_net_manager_attr_t));
    if (!attr_array)
    {
        result = GlobusNetManagerErrorMemory("attr_array");
        goto attr_array_malloc_fail;
    }
    for (i = 0; i < list_size; i++)
    {
        PyObject                       *pyattr_tuple = NULL;
        PyObject                       *pyscope = NULL,
                                       *pyname = NULL,
                                       *pyvalue = NULL;

        pyattr_tuple = PyList_GetItem(pylist, i);
        if (pyattr_tuple == NULL || !PyTuple_Check(pyattr_tuple))
        {
            result = GlobusNetManagerErrorParameter("pyattr_tuple");
            goto bad_tuple_item;
        }
        if (PyTuple_Size(pyattr_tuple) != 3)
        {
            result = GlobusNetManagerErrorParameter("pyattr_tuple");
            goto bad_tuple_size;
        }
        pyscope = PyTuple_GetItem(pyattr_tuple, 0);
        if (pyscope == NULL || !PyString_Check(pyscope))
        {
            result = GlobusNetManagerErrorParameter("pyscope");
            goto bad_tuple_scope;
        }
        pyname = PyTuple_GetItem(pyattr_tuple, 1);
        if (pyname == NULL || !PyString_Check(pyname))
        {
            result = GlobusNetManagerErrorParameter("pyname");
            goto bad_tuple_name;
        }
        pyvalue = PyTuple_GetItem(pyattr_tuple, 2);
        if (pyvalue == NULL || !PyString_Check(pyvalue))
        {
            result = GlobusNetManagerErrorParameter("pyvalue");
            goto bad_tuple_value;
        }
        attr_array[i].scope = strdup(PyString_AsString(pyscope));
        if (attr_array[i].scope == NULL)
        {
            result = GlobusNetManagerErrorMemory("scope");
            goto strdup_scope_fail;
        }
        attr_array[i].name = strdup(PyString_AsString(pyname));
        if (attr_array[i].name == NULL)
        {
            result = GlobusNetManagerErrorMemory("name");
            goto strdup_name_fail;
        }
        attr_array[i].value = strdup(PyString_AsString(pyvalue));
        if (attr_array[i].value == NULL)
        {
            result = GlobusNetManagerErrorMemory("value");
            goto strdup_value_fail;
        }
    }
    attr_array[list_size] = globus_net_manager_null_attr;
    if (result != GLOBUS_SUCCESS)
    {
        for (; i >= 0 ; i--)
        {
            free(attr_array[i].value);
strdup_value_fail:
            free(attr_array[i].name);
strdup_name_fail:
            free(attr_array[i].scope);
strdup_scope_fail:
bad_tuple_value:
bad_tuple_name:
bad_tuple_scope:
bad_tuple_size:
bad_tuple_item:
            ;
        }
        free(attr_array);
        attr_array = NULL;
    }
attr_array_malloc_fail:
    *attr_array_out = attr_array;
    return result;
}
/* globus_l_python_pylist_to_attr_array() */

static
globus_result_t
globus_l_net_manager_python_handle_exception(
    const char                         *func_name)
{
    globus_result_t                     result = GLOBUS_SUCCESS;
    PyObject                           *exc_type = NULL,
                                       *exc_value = NULL,
                                       *exc_trace = NULL,
                                       *exc_type_string = NULL,
                                       *exc_value_string = NULL;
    const char                         *exc_type_cstr = NULL,
                                       *exc_value_cstr = NULL;

    if (PyErr_Occurred())
    {
        PyErr_Fetch(&exc_type, &exc_value, &exc_trace);
        PyErr_NormalizeException(&exc_type, &exc_value, &exc_trace);
        exc_type_string = PyObject_Str(exc_type);
        exc_value_string = PyObject_Str(exc_value);
        PyErr_Restore(exc_type, exc_value, exc_trace);

        if (exc_type_string && exc_value_string)
        {
            exc_type_cstr = PyString_AsString(exc_type_string);
            exc_value_cstr = PyString_AsString(exc_value_string);
            if (exc_type_cstr && exc_value_cstr)
            {
                char *error_explanation = globus_common_create_string(
                        "%s exception in %s: %s",
                        exc_type_cstr,
                        func_name,
                        exc_value_cstr);

                if (error_explanation != NULL)
                {
                    result = GlobusNetManagerErrorManager(
                            result,
                            "python",
                            error_explanation);
                    free(error_explanation);
                }
            }
        }
        Py_DECREF(exc_type_string);
        Py_DECREF(exc_value_string);

        if (result == GLOBUS_SUCCESS)
        {
            result = GlobusNetManagerErrorManager(
                    result,
                    "python",
                    "Python exception occurred");
        }
    }
    return result;
}
/* globus_l_net_manager_python_handle_exception() */

/**
 * @brief Prepare arguments for calling python function
 * @details
 * This function create a python tuple containing the string arguments and
 * attributes in the python calling convention.
 *
 * @param[in] string_arg_count
 *     Number of elements in string_args
 * @param[in] string_args
 *     String parameters
 * @param[in] attr_array
 *     Attribute parameter
 * @param[out] pyargs_out
 *     Pointer to a python object to be set to the function parameters.
 *
 * @return
 *     On success, this function sets 'pyargs_out' to the parameters in python
 *     form and returns GLOBUS_SUCCESS.
 *     On failure, this function sets 'pyargs_out' to NULL and returns an
 *     error result.
 */
static
globus_result_t
globus_l_python_prep_args(
    size_t                              string_arg_count,
    const char *                        string_args[],
    const globus_net_manager_attr_t    *attr_array,
    PyObject                          **pyargs_out)
{
    PyObject                           *pyargs = NULL,
                                       *pystr = NULL,
                                       *pylist = NULL;
    globus_result_t                     result = GLOBUS_SUCCESS;

    pyargs = PyTuple_New(string_arg_count + 1);
    if (pyargs == NULL)
    {
        result = GlobusNetManagerErrorMemory("pyargs");
        goto pyargs_new_fail;
    }
    for (int i = 0; i < string_arg_count; i++)
    { 
        pystr = PyString_FromString(string_args[i]); 
        if (pystr == NULL)
        {
            result = GlobusNetManagerErrorMemory("pystr");
            goto pystr_from_string_failed;
        }
        PyTuple_SetItem(pyargs, i, pystr);
    }

    result = globus_l_python_attr_array_to_pylist(attr_array, &pylist);
    if (result)
    {
        goto attr_to_pylist_fail;
    }
    PyTuple_SetItem(pyargs, string_arg_count, pylist);

    if (result != GLOBUS_SUCCESS)
    {
attr_to_pylist_fail:
pystr_from_string_failed:
        Py_DECREF(pyargs);
        pyargs = NULL;
    }
pyargs_new_fail:
    *pyargs_out = pyargs;
    return result;
}
/* globus_l_python_prep_args() */

/**
 * @brief Parse response from calling python function
 * @details
 * This function parses a python tuple containing the string responses and
 * attributes from the python calling convention.
 *
 * @param[in] pyresult
 *     Pointer to a python object containing the response from the function.
 * @param[in] string_arg_count
 *     Number of elements in string_args
 * @param[out] string_args
 *     Pointer to an array of strings to be set to the string response
 *     values.
 * @param[out] attr_array
 *     Pointer to be set to an array of attributes returned in the response.
 *
 * @return
 *     On success, this function sets the strings pointed to by string_args
 *     to the response values or NULL, and sets attr_array to the response
 *     attributes, and returns GLOBUS_SUCCESS.
 *     On failure, this function sets all elements of string_args and
 *     attr_array to NULL and returns an error result.
 */
static
globus_result_t
globus_l_python_parse_response(
    PyObject                           *pyresult,
    size_t                              string_arg_count,
    char **                             string_args_out[],
    globus_net_manager_attr_t          **attr_array_out)
{
    globus_result_t                     result = GLOBUS_SUCCESS;
    Py_ssize_t                          expected_tuple_size;

    for (int i = 0; i < string_arg_count; i++)
    {
        *(string_args_out[i]) = NULL;
    }
    expected_tuple_size = string_arg_count;
    if (attr_array_out != NULL)
    {
        expected_tuple_size++;
        *attr_array_out = NULL;
    }

    if (pyresult && pyresult != Py_None)
    {
        if (string_arg_count > 0)
        {
            if (PyTuple_Check(pyresult) &&
                PyTuple_Size(pyresult) == expected_tuple_size)
            {
                for (int i = 0; i < string_arg_count; i++)
                {
                    PyObject               *pystr = NULL;

                    pystr = PyTuple_GetItem(pyresult, i);
                    if (!pystr)
                    {
                        result = GlobusNetManagerErrorParameter("pystr");
                        goto get_pystr_fail;
                    }
                    if (PyString_Check(pystr))
                    {
                        const char * tmpstr = PyString_AsString(pystr);
                        if (tmpstr)
                        {
                            *(string_args_out[i]) = strdup(tmpstr);

                            if (*(string_args_out[i]) == NULL)
                            {
                                goto strdup_tmpstr_fail;
                            }
                        }
                    }
                }
                if (attr_array_out != NULL)
                {
                    PyObject               *pyarray = NULL;

                    pyarray = PyTuple_GetItem(pyresult, string_arg_count);
                    if (!pyarray)
                    {
                        result = GlobusNetManagerErrorParameter("pyarray");
                        goto get_attr_array_out_fail;
                    }
                    if (pyarray == Py_None)
                    {
                        *attr_array_out = NULL;
                    }
                    else if (PyList_Check(pyarray))
                    {
                        result = globus_l_python_pylist_to_attr_array(
                            pyarray, attr_array_out);
                    }
                    else
                    {
                        result = GlobusNetManagerErrorParameter("pyarray");
                        goto py_attr_array_not_list;
                    }
                }
            }
        }
        else if (attr_array_out != NULL)
        {
            if (PyList_Check(pyresult))
            {
                result = globus_l_python_pylist_to_attr_array(
                    pyresult, attr_array_out);
            }
            else
            {
                result = GlobusNetManagerErrorParameter("pyresult");
                goto py_attr_array_not_list;
            }
        }
        else
        {
            result = GlobusNetManagerErrorParameter("pyresult");
            goto py_result_wrong_size;
        }
        if (result != GLOBUS_SUCCESS)
        {
strdup_tmpstr_fail:
get_pystr_fail:
py_result_wrong_size:
py_attr_array_not_list:
get_attr_array_out_fail:
            for (int i = 0; i < string_arg_count; i++)
            {
                free(*(string_args_out[i]));
            }
        }
        ;
    }
    return result;
}
/* globus_l_python_parse_response() */

static
globus_result_t
globus_l_python_pre_listen(
    struct globus_net_manager_s        *manager,
    const globus_net_manager_attr_t    *manager_attr_array,
    const char                         *task_id,
    const char                         *transport,
    const globus_net_manager_attr_t    *attr_array,
    globus_net_manager_attr_t         **attr_array_out)
{
    globus_result_t                     result = GLOBUS_SUCCESS;
    globus_l_python_modref_t           *pymod = NULL;

    result = globus_l_python_module(manager_attr_array, &pymod);

    if (result)
    {
        goto lookup_module_fail;
    }
    assert(pymod != NULL);

    if (pymod->pre_listen)
    {
        PyObject                       *pyargs = NULL,
                                       *pyresult = NULL;

        result = globus_l_python_prep_args(
            2,
            (const char*[2]) { task_id, transport },
            attr_array,
            &pyargs);
        if (result != GLOBUS_SUCCESS)
        {
            goto prep_args_fail;
        }

        PyErr_Clear();
        pyresult = PyObject_CallObject(pymod->pre_listen, pyargs);

        result = globus_l_python_parse_response(
                pyresult,
                0,
                NULL,
                attr_array_out);
        if (pyresult)
        {
            Py_DECREF(pyresult);
        }

        if (result == GLOBUS_SUCCESS)
        {
            result = globus_l_net_manager_python_handle_exception("pre_listen");
        }
        Py_DECREF(pyargs);
    }
prep_args_fail:
lookup_module_fail:
    return result;
}
/* globus_l_python_pre_listen() */

static
globus_result_t
globus_l_python_post_listen(
    struct globus_net_manager_s        *manager,
    const globus_net_manager_attr_t    *manager_attr_array,
    const char                         *task_id,
    const char                         *transport,
    const char                         *local_contact,
    const globus_net_manager_attr_t    *attr_array,
    char                              **local_contact_out,
    globus_net_manager_attr_t         **attr_array_out)
{
    globus_result_t                     result = GLOBUS_SUCCESS;
    globus_l_python_modref_t           *pymod = NULL;

    if (local_contact_out == NULL)
    {
        result = GlobusNetManagerErrorParameter("local_contact_out");
        goto local_contact_null;
    }
    *local_contact_out = NULL;

    if (attr_array_out == NULL)
    {
        result = GlobusNetManagerErrorParameter("attr_array_out");
        goto attr_array_out_null;
    }
    *attr_array_out = NULL;

    result = globus_l_python_module(manager_attr_array, &pymod);
    if (result)
    {
        goto lookup_module_fail;
    }
    assert(pymod != NULL);

    if (pymod->post_listen)
    {
        PyObject                       *pyargs = NULL,
                                       *pyresult = NULL;

        result = globus_l_python_prep_args(
            3,
            (const char*[3]) {
                task_id,
                transport,
                local_contact
            },
            attr_array,
            &pyargs);

        if (result != GLOBUS_SUCCESS)
        {
            goto prep_args_fail;
        }

        PyErr_Clear();
        pyresult = PyObject_CallObject(pymod->post_listen, pyargs);

        result = globus_l_python_parse_response(
                pyresult,
                1,
                (char **[1]) {local_contact_out},
                attr_array_out);

        if (pyresult)
        {
            Py_DECREF(pyresult);
        }

        if (result == GLOBUS_SUCCESS)
        {
            result = globus_l_net_manager_python_handle_exception(
                    "post_listen");
        }
        Py_DECREF(pyargs);
    }
prep_args_fail:
lookup_module_fail:
attr_array_out_null:
local_contact_null:
    return result;
}
/* globus_l_python_post_listen() */


static
globus_result_t
globus_l_python_end_listen(
    struct globus_net_manager_s        *manager,
    const globus_net_manager_attr_t    *manager_attr_array,
    const char                         *task_id,
    const char                         *transport,
    const char                         *local_contact,
    const globus_net_manager_attr_t    *attr_array)
{
    globus_result_t                     result = GLOBUS_SUCCESS;
    globus_l_python_modref_t           *pymod = NULL;

    result = globus_l_python_module(manager_attr_array, &pymod);
    if (result)
    {
        goto lookup_module_fail;
    }
    assert(pymod != NULL);

    if (pymod->end_listen)
    {
        PyObject                       *pyargs = NULL,
                                       *pyresult = NULL;

        result = globus_l_python_prep_args(
            3,
            (const char*[3]) {
                task_id,
                transport,
                local_contact
            },
            attr_array,
            &pyargs);

        if (result != GLOBUS_SUCCESS)
        {
            goto prep_args_fail;
        }

        PyErr_Clear();
        pyresult = PyObject_CallObject(pymod->end_listen, pyargs);

        result = globus_l_python_parse_response(
                pyresult,
                0,
                NULL,
                NULL);

        if (pyresult)
        {
            Py_DECREF(pyresult);
        }

        if (result == GLOBUS_SUCCESS)
        {
            result = globus_l_net_manager_python_handle_exception(
                    "pre_end");
        }
        Py_DECREF(pyargs);
        pyargs = NULL;
    }
prep_args_fail:
lookup_module_fail:
    return result;
}
/* globus_l_python_end_listen() */

static
globus_result_t
globus_l_python_pre_accept(
    struct globus_net_manager_s        *manager,
    const globus_net_manager_attr_t    *manager_attr_array,
    const char                         *task_id,
    const char                         *transport,
    const char                         *local_contact,
    const globus_net_manager_attr_t    *attr_array,
    globus_net_manager_attr_t         **attr_array_out)
{
    globus_result_t                     result = GLOBUS_SUCCESS;
    globus_l_python_modref_t           *pymod = NULL;

    result = globus_l_python_module(manager_attr_array, &pymod);

    if (result)
    {
        goto lookup_module_fail;
    }
    assert(pymod != NULL);

    if (pymod->pre_accept)
    {
        PyObject                       *pyargs = NULL,
                                       *pyresult = NULL;

        result = globus_l_python_prep_args(
            3,
            (const char*[3]) {
                task_id,
                transport,
                local_contact
            },
            attr_array,
            &pyargs);
        if (result != GLOBUS_SUCCESS)
        {
            goto prep_args_fail;
        }

        PyErr_Clear();

        pyresult = PyObject_CallObject(pymod->pre_accept, pyargs);

        result = globus_l_python_parse_response(
                pyresult,
                0,
                NULL,
                attr_array_out);

        if (pyresult)
        {
            Py_DECREF(pyresult);
        }
        if (result == GLOBUS_SUCCESS)
        {
            result = globus_l_net_manager_python_handle_exception(
                    "pre_accept");
        }
        Py_DECREF(pyargs);
        pyargs = NULL;
    }
prep_args_fail:
lookup_module_fail:
    return result;
}
/* globus_l_python_pre_accept() */

static
globus_result_t
globus_l_python_post_accept(
    struct globus_net_manager_s        *manager,
    const globus_net_manager_attr_t    *manager_attr_array,
    const char                         *task_id,
    const char                         *transport,
    const char                         *local_contact,
    const char                         *remote_contact,
    const globus_net_manager_attr_t    *attr_array,
    globus_net_manager_attr_t         **attr_array_out)
{
    globus_result_t                     result = GLOBUS_SUCCESS;
    globus_l_python_modref_t           *pymod = NULL;

    result = globus_l_python_module(manager_attr_array, &pymod);
    if (result)
    {
        goto lookup_module_fail;
    }
    assert(pymod != NULL);

    if (pymod->post_accept)
    {
        PyObject                       *pyargs = NULL,
                                       *pyresult = NULL;

        result = globus_l_python_prep_args(
            4,
            (const char*[4]) {
                task_id,
                transport,
                local_contact,
                remote_contact
            },
            attr_array,
            &pyargs);
        if (result != GLOBUS_SUCCESS)
        {
            goto prep_args_fail;
        }

        PyErr_Clear();
        pyresult = PyObject_CallObject(pymod->post_accept, pyargs);
        result = globus_l_python_parse_response(
                pyresult,
                0,
                NULL,
                attr_array_out);
        if (pyresult)
        {
            Py_DECREF(pyresult);
        }
        if (result == GLOBUS_SUCCESS)
        {
            result = globus_l_net_manager_python_handle_exception(
                    "post_accept");
        }
        Py_DECREF(pyargs);
        pyargs = NULL;
    }
prep_args_fail:
lookup_module_fail:
    return result;
}
/* globus_l_python_post_accept() */

static
globus_result_t
globus_l_python_pre_connect(
    struct globus_net_manager_s        *manager,
    const globus_net_manager_attr_t    *manager_attr_array,
    const char                         *task_id,
    const char                         *transport,
    const char                         *remote_contact,
    const globus_net_manager_attr_t    *attr_array,
    char                              **remote_contact_out,
    globus_net_manager_attr_t         **attr_array_out)
{
    globus_result_t                     result = GLOBUS_SUCCESS;
    globus_l_python_modref_t           *pymod = NULL;

    if (remote_contact_out == NULL)
    {
        result = GlobusNetManagerErrorParameter("remote_contact_out");
        goto remote_contact_null;
    }
    *remote_contact_out = NULL;

    if (attr_array_out == NULL)
    {
        result = GlobusNetManagerErrorParameter("attr_array_out");
        goto attr_array_out_null;
    }
    *attr_array_out = NULL;

    result = globus_l_python_module(manager_attr_array, &pymod);
    if (result)
    {
        goto lookup_module_fail;
    }
    assert(pymod != NULL);

    if (pymod->pre_connect)
    {
        PyObject                       *pyargs = NULL,
                                       *pyresult = NULL;

        result = globus_l_python_prep_args(
            3,
            (const char*[3]) { task_id, transport, remote_contact },
            attr_array,
            &pyargs);
        if (result != GLOBUS_SUCCESS)
        {
            goto prep_args_fail;
        }

        PyErr_Clear();
        pyresult = PyObject_CallObject(pymod->pre_connect, pyargs);

        result = globus_l_python_parse_response(
                pyresult,
                1,
                (char **[]) { remote_contact_out },
                attr_array_out);
        if (pyresult)
        {
            Py_DECREF(pyresult);
        }

        if (result == GLOBUS_SUCCESS)
        {
            result = globus_l_net_manager_python_handle_exception(
                    "pre_connect");
        }
        Py_DECREF(pyargs);
        pyargs = NULL;
    }
prep_args_fail:
lookup_module_fail:
attr_array_out_null:
remote_contact_null:
    return result;
}
/* globus_l_python_pre_connect() */

static
globus_result_t
globus_l_python_post_connect(
    struct globus_net_manager_s        *manager,
    const globus_net_manager_attr_t    *manager_attr_array,
    const char                         *task_id,
    const char                         *transport,
    const char                         *local_contact,
    const char                         *remote_contact,
    const globus_net_manager_attr_t    *attr_array,
    globus_net_manager_attr_t         **attr_array_out)
{
    globus_result_t                     result = GLOBUS_SUCCESS;
    globus_l_python_modref_t           *pymod = NULL;

    result = globus_l_python_module(manager_attr_array, &pymod);

    if (result)
    {
        goto lookup_module_fail;
    }
    assert(pymod != NULL);

    if (pymod->post_connect)
    {
        PyObject                       *pyargs = NULL,
                                       *pyresult = NULL;

        result = globus_l_python_prep_args(
            4,
            (const char*[4]) {
                task_id,
                transport,
                local_contact,
                remote_contact
            },
            attr_array,
            &pyargs);
        if (result != GLOBUS_SUCCESS)
        {
            goto prep_args_fail;
        }

        PyErr_Clear();
        pyresult = PyObject_CallObject(pymod->post_connect, pyargs);

        result = globus_l_python_parse_response(
                pyresult,
                0,
                NULL,
                attr_array_out);
        if (pyresult)
        {
            Py_DECREF(pyresult);
        }

        if (result == GLOBUS_SUCCESS)
        {
            result = globus_l_net_manager_python_handle_exception(
                    "post_connect");
        }
        Py_DECREF(pyargs);
        pyargs = NULL;
    }
prep_args_fail:
lookup_module_fail:
    return result;
}
/* globus_l_python_post_connect() */

static
globus_result_t
globus_l_python_pre_close(
    struct globus_net_manager_s        *manager,
    const globus_net_manager_attr_t    *manager_attr_array,
    const char                         *task_id,
    const char                         *transport,
    const char                         *local_contact,
    const char                         *remote_contact,
    const globus_net_manager_attr_t    *attr_array)
{
    globus_result_t                     result = GLOBUS_SUCCESS;
    globus_l_python_modref_t           *pymod = NULL;

    result = globus_l_python_module(manager_attr_array, &pymod);

    if (result)
    {
        goto lookup_module_fail;
    }
    assert(pymod != NULL);

    if (pymod->pre_close)
    {
        PyObject                       *pyargs = NULL,
                                       *pyresult = NULL;

        result = globus_l_python_prep_args(
            4,
            (const char*[4]) {
                task_id,
                transport,
                local_contact,
                remote_contact
            },
            attr_array,
            &pyargs);
        if (result != GLOBUS_SUCCESS)
        {
            goto prep_args_fail;
        }

        PyErr_Clear();
        pyresult = PyObject_CallObject(pymod->pre_close, pyargs);

        result = globus_l_python_parse_response(
                pyresult,
                0,
                NULL,
                NULL);
        if (pyresult)
        {
            Py_DECREF(pyresult);
        }

        if (result == GLOBUS_SUCCESS)
        {
            result = globus_l_net_manager_python_handle_exception(
                    "pre_close");
        }
        Py_DECREF(pyargs);
        pyargs = NULL;
    }
prep_args_fail:
lookup_module_fail:
    return result;
}
/* globus_l_python_pre_close() */

static
globus_result_t
globus_l_python_post_close(
    struct globus_net_manager_s        *manager,
    const globus_net_manager_attr_t    *manager_attr_array,
    const char                         *task_id,
    const char                         *transport,
    const char                         *local_contact,
    const char                         *remote_contact,
    const globus_net_manager_attr_t    *attr_array)
{
    globus_result_t                     result = GLOBUS_SUCCESS;
    globus_l_python_modref_t           *pymod = NULL;

    result = globus_l_python_module(manager_attr_array, &pymod);

    if (result)
    {
        goto lookup_module_fail;
    }
    assert(pymod != NULL);

    if (pymod->post_close)
    {
        PyObject                       *pyargs = NULL,
                                       *pyresult = NULL;

        result = globus_l_python_prep_args(
            4,
            (const char*[4]) {
                task_id,
                transport,
                local_contact,
                remote_contact
            },
            attr_array,
            &pyargs);
        if (result != GLOBUS_SUCCESS)
        {
            goto prep_args_fail;
        }

        PyErr_Clear();
        pyresult = PyObject_CallObject(pymod->post_close, pyargs);

        result = globus_l_python_parse_response(
                pyresult,
                0,
                NULL,
                NULL);
        if (pyresult)
        {
            Py_DECREF(pyresult);
        }
        if (result == GLOBUS_SUCCESS)
        {
            result = globus_l_net_manager_python_handle_exception(
                    "post_close");
        }
        Py_DECREF(pyargs);
        pyargs = NULL;
    }
prep_args_fail:
lookup_module_fail:
    return result;
}
/* globus_l_python_post_close() */

static int globus_l_net_manager_python_activate(void);
static int globus_l_net_manager_python_deactivate(void);

static globus_net_manager_t globus_l_net_manager_python = {
    "python",
    globus_l_python_pre_listen,
    globus_l_python_post_listen,
    globus_l_python_end_listen,
    globus_l_python_pre_accept,
    globus_l_python_post_accept,
    globus_l_python_pre_connect,
    globus_l_python_post_connect,
    globus_l_python_pre_close,
    globus_l_python_post_close
};

static
void *
globus_l_net_manager_python_get_pointer(void)
{
    return &globus_l_net_manager_python;
}

GlobusExtensionDefineModule(globus_net_manager_python) =
{
    "globus_net_manager_python",
    globus_l_net_manager_python_activate,
    globus_l_net_manager_python_deactivate,
    NULL,
    globus_l_net_manager_python_get_pointer,
    &local_version
};
static
int
globus_l_net_manager_python_activate(void)
{
    Py_Initialize();
    globus_mutex_init(&globus_l_python_modules_lock, NULL);
    globus_hashtable_init(
            &globus_l_python_modules,
            7,
            globus_hashtable_string_hash, 
            globus_hashtable_string_keyeq); 
    globus_module_activate(GLOBUS_NET_MANAGER_MODULE);
    return globus_net_manager_register(
        &globus_l_net_manager_python,
        GlobusExtensionMyModule(globus_net_manager_python));
}

static
void
globus_l_python_modules_destroy(void *datum)
{
    globus_l_python_modref_t           *modref = datum;
    if (modref)
    {
        free(modref->key);
        Py_XDECREF(modref->module);
        Py_XDECREF(modref->pre_listen);
        Py_XDECREF(modref->post_listen);
        Py_XDECREF(modref->end_listen);
        Py_XDECREF(modref->pre_accept);
        Py_XDECREF(modref->post_accept);
        Py_XDECREF(modref->pre_connect);
        Py_XDECREF(modref->post_connect);
        Py_XDECREF(modref->pre_close);
        Py_XDECREF(modref->post_close);
        free(modref);
    }
}
/* globus_l_python_modules_destroy() */

static
int
globus_l_net_manager_python_deactivate(void)
{
    globus_net_manager_unregister(&globus_l_net_manager_python);
    globus_hashtable_destroy_all(
            &globus_l_python_modules,
            globus_l_python_modules_destroy);
    Py_Finalize();
    return globus_module_deactivate(GLOBUS_NET_MANAGER_MODULE);
}
