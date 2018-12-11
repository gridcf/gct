MODULE_NAME = "return_function_called_module"
FUNCTION = "function"

def pre_listen(task_id, transport, attrs):
    return [(MODULE_NAME, FUNCTION, "pre_listen")]

def post_listen(task_id, transport, local_contact, attrs):
    return '', [(MODULE_NAME, FUNCTION, "post_listen")]

def end_listen(task_id, transport, local_contact, attrs):
    return

def pre_accept(task_id, transport, local_contact, attrs):
    return [(MODULE_NAME, FUNCTION, "pre_accept")]

def post_accept(task_id, transport, local_contact, remote_contact, attrs):
    return [(MODULE_NAME, FUNCTION, "post_accept")]

def pre_connect(task_id, transport, remote_contact, attrs):
    return '', [(MODULE_NAME, FUNCTION, "pre_connect")]

def post_connect(task_id, transport, local_contact, remote_contact, attrs):
    return [(MODULE_NAME, FUNCTION, "post_connect")]

def pre_close(task_id, transport, local_contact, remote_contact, attrs):
    return

def post_close(task_id, transport, local_contact, remote_contact, attrs):
    return
