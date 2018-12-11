from __future__ import print_function

def pre_listen(task_id, transport, attrs):
    res = None
    for (scope, name, value) in attrs:
        print("# (%s, %s, %s)" % (scope, name, value))
        if scope == 'python':
            if name == 'test_func' and value != "pre_listen":
                raise RuntimeError("pre_listen called when %s expected" % value)
            elif name == 'expected_result':
                print("# evaluating", value)
                ns = {}; exec(value, ns); res = ns['res']
    print("# res =", res)
    return res

def post_listen(task_id, transport, local_contact, attrs):
    print("# task_id =", str(task_id))
    print("# transport =", str(transport))
    print("# local_contact =", str(local_contact))
    print("# attrs =", str(attrs))
    res = None
    for (scope, name, value) in attrs:
        print("# (%s, %s, %s)" % (scope, name, value))
        if scope == 'python':
            if name == 'test_func' and value != "post_listen":
                raise RuntimeError("post_listen called when %s expected" % value)
            elif name == 'expected_result':
                print("# evaluating", value)
                ns = {}; exec(value, ns); res = ns['res']
    print("# res =", res)
    return res

def end_listen(task_id, transport, local_contact, attrs):
    print("# task_id =", str(task_id))
    print("# transport =", str(transport))
    print("# local_contact =", str(local_contact))
    print("# attrs =", str(attrs))
    res = None
    for (scope, name, value) in attrs:
        print("# (%s, %s, %s)" % (scope, name, value))
        if scope == 'python':
            if name == 'test_func' and value != "end_listen":
                raise RuntimeError("post_listen called when %s expected" % value)
            elif name == 'expected_result':
                print("# evaluating", value)
                ns = {}; exec(value, ns); res = ns['res']
    print("# res =", res)
    return res

def pre_accept(task_id, transport, local_contact, attrs):
    print("# task_id =", str(task_id))
    print("# transport =", str(transport))
    print("# local_contact =", str(local_contact))
    print("# attrs =", str(attrs))
    res = None
    for (scope, name, value) in attrs:
        print("# (%s, %s, %s)" % (scope, name, value))
        if scope == 'python':
            if name == 'test_func' and value != "pre_accept":
                raise RuntimeError("pre_accept called when %s expected" % value)
            elif name == 'expected_result':
                print("# evaluating", value)
                ns = {}; exec(value, ns); res = ns['res']
    print("# res =", res)
    return res


def post_accept(task_id, transport, local_contact, remote_contact, attrs):
    print("# task_id =", str(task_id))
    print("# transport =", str(transport))
    print("# local_contact =", str(local_contact))
    print("# remote_contact =", str(remote_contact))
    print("# attrs =", str(attrs))
    res = None
    for (scope, name, value) in attrs:
        print("# (%s, %s, %s)" % (scope, name, value))
        if scope == 'python':
            if name == 'test_func' and value != "post_accept":
                raise RuntimeError("post_accept called when %s expected" % value)
            elif name == 'expected_result':
                print("# evaluating", value)
                ns = {}; exec(value, ns); res = ns['res']
    print("# res =", res)
    return res

def pre_connect(task_id, transport, remote_contact, attrs):
    print("# task_id =", str(task_id))
    print("# transport =", str(transport))
    print("# remote_contact =", str(remote_contact))
    print("# attrs =", str(attrs))
    res = None
    for (scope, name, value) in attrs:
        print("# (%s, %s, %s)" % (scope, name, value))
        if scope == 'python':
            if name == 'test_func' and value != "pre_connect":
                raise RuntimeError("pre_connect called when %s expected" % value)
            elif name == 'expected_result':
                print("# evaluating", value)
                ns = {}; exec(value, ns); res = ns['res']
    print("# res =", res)
    return res

def post_connect(task_id, transport, local_contact, remote_contact, attrs):
    print("# task_id =", str(task_id))
    print("# transport =", str(transport))
    print("# local_contact =", str(local_contact))
    print("# remote_contact =", str(remote_contact))
    print("# attrs =", str(attrs))
    res = None
    for (scope, name, value) in attrs:
        print("# (%s, %s, %s)" % (scope, name, value))
        if scope == 'python':
            if name == 'test_func' and value != "post_connect":
                raise RuntimeError("post_connect called when %s expected" % value)
            elif name == 'expected_result':
                print("# evaluating", value)
                ns = {}; exec(value, ns); res = ns['res']
    print("# res =", res)
    return res

def pre_close(task_id, transport, local_contact, remote_contact, attrs):
    print("# task_id =", str(task_id))
    print("# transport =", str(transport))
    print("# local_contact =", str(local_contact))
    print("# remote_contact =", str(remote_contact))
    print("# attrs =", str(attrs))
    res = None
    for (scope, name, value) in attrs:
        print("# (%s, %s, %s)" % (scope, name, value))
        if scope == 'python':
            if name == 'test_func' and value != "pre_close":
                raise RuntimeError("pre_close called when %s expected" % value)
            elif name == 'expected_result':
                print("# evaluating", value)
                ns = {}; exec(value, ns); res = ns['res']
    print("# res =", res)
    return res

def post_close(task_id, transport, local_contact, remote_contact, attrs):
    print("# task_id =", str(task_id))
    print("# transport =", str(transport))
    print("# local_contact =", str(local_contact))
    print("# remote_contact =", str(remote_contact))
    print("# attrs =", str(attrs))
    res = None
    for (scope, name, value) in attrs:
        print("# (%s, %s, %s)" % (scope, name, value))
        if scope == 'python':
            if name == 'test_func' and value != "post_close":
                raise RuntimeError("post_close called when %s expected" % value)
            elif name == 'expected_result':
                print("# evaluating", value)
                ns = {}; exec(value, ns); res = ns['res']
    print("# res =", res)
    return res
