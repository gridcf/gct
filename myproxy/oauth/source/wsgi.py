#! /usr/bin/python2
#
# Copyright 2010-2011 University of Chicago
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
# http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
#

import os
import pwd
import sys
import socket
import logging
import getopt

BASE_DIR = os.path.abspath(os.path.dirname(__file__))

sys.path.insert(0, BASE_DIR)

root = "/oauth"

import ssl
from wsgiref.simple_server import WSGIServer, WSGIRequestHandler


class SecureWSGIServer(WSGIServer):
    def set_credentials( self, keypath=None, certpath=None):
        self.keypath = keypath
        self.certpath = certpath

    def get_request(self):
        (sock, addr) = WSGIServer.get_request(self)
        ssock = ssl.wrap_socket(sock,
            keyfile=self.keypath, certfile=self.certpath, server_side=True)
        return (ssock, addr)

    def close_request(self, request):
        try:
            request.shutdown(socket.SHUT_RDWR) 
            request.close() 
        except Exception, e: 
            print "Exception closing request" + str(e) 

class SecureWSGIRequestHandler( WSGIRequestHandler):
    """An SSL-aware WSGIRequestHandler, which sets HTTPS environment variables.
    """
    #xxx todo: set SSL_PROTOCOL, maybe others
    def get_environ( self):
        env = WSGIRequestHandler.get_environ( self)
        if isinstance( self.request, ssl.SSLSocket):
            env['HTTPS'] = 'on'
            # JB: Add handling of wsgi.url_scheme
            env['wsgi.url_scheme'] = 'https'
        # JB: Add handling of script name
        if env['SCRIPT_NAME'] == "" and env['PATH_INFO'].startswith(root):
            env['SCRIPT_NAME'] = root
            env['PATH_INFO'] = env['PATH_INFO'][len(root):]
        return env

if __name__ == "__main__":
    opts, args = getopt.getopt(sys.argv[1:],"c:k:u:p:bf:l:h")
    certfile = None
    keyfile = None
    user = None
    port = 443
    pidfile = None
    log_level = logging.WARN
    background = False
    for (opt, param) in opts:
        if opt == '-c':
            certfile = param
        elif opt == '-k':
            keyfile = param
        elif opt == '-u':
            user = param
        elif opt == '-p':
            port = int(param)
        elif opt == '-b':
            background = True
        elif opt == '-f':
            pidfile = param
        elif opt == '-l':
            log_level_str = param
            log_level = getattr(logging, log_level_str)
        elif opt == '-h':
            print "Usage %s [-c CERT-FILE] [-k KEYFILE] [-u USER] [-p PORT] | [-h]\n" %(sys.argv[0])
            sys.exit(0)
    if certfile is None:
        certfile = os.environ.get("X509_USER_CERT")
    if certfile is None:
        certfile = "/etc/grid-security/hostcert.pem"
    if keyfile is None:
        keyfile = os.environ.get("X509_USER_KEY")
    if keyfile is None:
        keyfile = "/etc/grid-security/hostkey.pem"

    pid = 0
    if background:
        pid = os.fork()
        if pid < 0:
            print >>sys.stderr, "Error forking background process"
            sys.exit(1)
        elif pid > 0:
            print "Running in background (%d)" % (pid)
            if pidfile is not None:
                pid_fd = file(pidfile, "w")
                try:
                    pid_fd.write("%d\n" % (pid))
                finally:
                    pid_fd.close()
                sys.exit(0);
        elif pid == 0:
            os.setsid()
            os.close(sys.stdin.fileno())
            sys.stdin = open(os.devnull, "r")
            os.close(sys.stdout.fileno())
            sys.stdout = open(os.devnull, "a")
            os.close(sys.stderr.fileno())
            sys.stderr = open(os.devnull, "a")

    server = SecureWSGIServer(("0.0.0.0", port), SecureWSGIRequestHandler)
    if pidfile is not None:
        f = file(pidfile, "w")
        try:
            print >>f, str(os.getpid())
        finally:
            f.close()
    if os.getuid() == 0 and user is not None:
        os.seteuid(pwd.getpwnam(user)[2])
    # import after setuid due to side-effects of import (creating db)
    from myproxyoauth import application
    application.logger.setLevel(log_level)
    server.set_app(application)
    server.set_credentials(keypath=keyfile,certpath=certfile)
    try:
        server.serve_forever()
    finally:
        if pidfile is not None:
            if os.getuid() != os.geteuid():
                os.setuid(os.getuid())
                os.path.remove(pidfile)
else:
    from myproxyoauth import application
# vim:filetype=python:nospell:
