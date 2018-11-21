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

#ifndef GLOBUS_NET_MANAGER_PYTHON_H
#define GLOBUS_NET_MANAGER_PYTHON_H 1

#include "globus_common.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @defgroup globus_net_manager_python Python Module
 * @ingroup globus_net_manager
 * @brief Net Manager Python Module
 *
 * @details
 * The Net Manager Python module is an example module that provides basic
 * Python language bindings to the Network Manager callout functionality. 
 * To use this example, define a python module that implements the some
 * subset of the following functions:
@code {.py}
def pre_listen(task_id, transport, attrs):
    # return list of (scope, name, value) tuples or None
def post_listen(task_id, transport, local_contact, attrs):
    # return a tuple containing (local_contact_out, [(scope, name, value),...])
def end_listen(task_id, transport, local_contact, attrs):
    # return a list of (scope, name, value) tuples or None
def pre_accept(task_id, transport, local_contact, attrs):
    # return list of (scope, name, value) tuples or None
def post_accept(task_id, transport, local_contact, remote_contact, attrs):
    # return list of (scope, name, value) tuples or None
def pre_connect(task_id, transport, remote_contact, attrs):
    # return a tuple containing (remote_contact_out, [(scope, name, value),...])
def post_connect(task_id, transport, local_contact, remote_contact, attrs):
    # return list of (scope, name, value) tuples or None
def pre_close(task_id, transport, local_contact, remote_contact, attrs):
    # return None
def post_close(task_id, transport, local_contact, remote_contact, attrs):
    # return None
@endcode
 * The *task_id*, *transport*, *local_contact*,
 * and *remote_contact* parameters to these functions are all string
 * objects.
 * The *attrs* parameter to these functions are lists of 3-tuples
 * (scope, name, value).
 *
 * To use this with the GridFTP server, add a file containing the following to
 * the GridFTP configuration directory <code>/etc/gridftp.d</code>:
@verbatim
$PYTHONPATH PATH
xnetmgr "manager=python;pymod=MODULE_NAME;"
@endverbatim
 * Where 'PATH' is the directory containing your module
 * and 'MODULE_NAME' is the name of the python module that you
 * would use to import it (i.e. without the '.py' extension). So a module
 * '/usr/local/globus/routeman.py' would require
@verbatim
$PYTHONPATH /usr/local/globus
xnetmgr "manager=python;pymod=routeman;"
@endverbatim
 *
 * To use this with the XIO module directly,
 * set the string options <code>manager=python;pymod=routeman;</code>.
 * You'll need to set the <code>PYTHONPATH</code> environment variable
 * elsewhere.
 *
 * To configure the network manager to use this module directly without
 * XIO, set the "pymod" attribute in the "python" scope to the name of the
 * python module to import and use. For example:
@code
globus_net_manager_attr_init(
    &attr,
    "python",
    "pymod",
    "routeman");
@endcode
 * and pass this to the context functions.
 */
GlobusExtensionDeclareModule(globus_net_manager_python);

#ifdef __cplusplus
}
#endif

#endif /* GLOBUS_NET_MANAGER_PYTHON_H */
