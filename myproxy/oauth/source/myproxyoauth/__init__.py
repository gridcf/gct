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

from myproxyoauth.database import init_db

init_db()

import logging, sys
import os
import pkgutil
import myproxyoauth.templates
import myproxyoauth.static

logging.basicConfig(stream=sys.stderr)

__path__ = pkgutil.extend_path(__path__, __name__)

class MyProxyOAuth(object):
    logger = logging.getLogger()

    def __init__(self):
        self.routes = dict()
        self.teardown_request_func = None
        self.logger = MyProxyOAuth.logger

    def __call__(self, environ, start_response):
        path_info = environ.get("PATH_INFO")
        method = environ.get("REQUEST_METHOD")
        template_route = "GET:/templates/"
        static_route = "GET:/static/"

        route = method + ":" + path_info
        self.logger.debug("route is " + route)
        if route in self.routes:
            self.logger.debug("route is present")
            exc = None
            try:
                try:
                    return self.routes[route](environ, start_response)
                except Exception as e:
                    headers = [("Content-Type", "text/plain")]
                    response = "500 Internal Server Error"
                    return str(e)
            finally:
                if self.teardown_request_func is not None:
                    self.teardown_request_func(exception=e)
        elif route.startswith(template_route) or route.startswith(static_route):
            self.logger.debug("Routing static content")
            dataname = None
            modname = None
            content_type = None
            if route.startswith(template_route):
                modname = 'myproxyoauth.templates'
                dataname = route[len(template_route):]
                content_type = "text/html"
            else:
                modname = 'myproxyoauth.static'
                dataname = route[len(static_route):]
                if dataname.endswith(".png"):
                    content_type = "image/png"
                elif dataname.endswith(".css"):
                    content_type = "text/css"
                else:
                    content_type = "application/octet-string"

            try: 
                if not("/" in dataname or 
                        ".py" in dataname or
                        dataname == "." or
                        dataname == ".."):
                    data = pkgutil.get_data(modname, dataname)
                    status = "200 Ok"
                    headers = [("Content-Type", content_type)]
                    start_response(status, headers)
                    return data
            except Exception as e:
                headers = [("Content-Type", "text/plain")]
                response = "500 Internal Server Error"
                return str(e)
        else:
            try:
                headers = [("Content-Type", "text/plain")]
                response = "404 Not Found"
                start_response(response, headers)
                return [response]
            finally:
                if self.teardown_request_func is not None:
                    self.teardown_request_func()

    def route(self, path, methods=["GET"]):
        def decorator(func):
            for m in methods:
                self.routes[m + ":" + path] = func
        return decorator

    def teardown_request(self, func):
        def decorator():
            self.teardown_request_func = func
        return decorator

application = MyProxyOAuth()

import myproxyoauth.views
# vim: filetype=python: nospell:
