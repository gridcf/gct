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

import socket
import cgi
import random
import base64
import http.client
import json
import time
import oauth2 as oauth
import Cryptodome.PublicKey.RSA
import myproxy
import pkgutil
import os
import sys
from myproxyoauth import application
from myproxyoauth.database import db_session, Admin, Client, Transaction
from urllib.parse import quote
from functools import reduce

def bad_request(start_response):
    status = "400 Bad Request"
    headers = [ ("Content-Type", "text/plain") ]
    start_response(status, headers, sys.exc_info())
    return "Bad request\n"

def get_template(name):
    template_data = None
    if hasattr(pkgutil, "get_data"):
        template_data = pkgutil.get_data("myproxyoauth.templates", name)
    else:
        template_path = os.path.join(
            os.path.dirname(__file__), 'templates', name)
        template_file = file(template_path, "r")
        try:
            template_data = template_file.read()
        finally:
            template_file.close()
    return template_data
        

def render_template(name, **kwargs):
    template = get_template(name)
    for template_token in kwargs:
        template = template.replace(
            "{{ " + template_token + " }}", kwargs[template_token])
    return str(template)

def url_reconstruct(environ):
    url = environ['wsgi.url_scheme']+'://'

    if environ.get('HTTP_HOST'):
        url += environ['HTTP_HOST']
    else:
        url += environ['SERVER_NAME']

        if environ['wsgi.url_scheme'] == 'https':
            if environ['SERVER_PORT'] != '443':
               url += ':' + environ['SERVER_PORT']
        else:
            if environ['SERVER_PORT'] != '80':
               url += ':' + environ['SERVER_PORT']

    url += quote(environ.get('SCRIPT_NAME', ''))
    url += quote(environ.get('PATH_INFO', ''))
    if environ.get('QUERY_STRING'):
        url += '?' + environ['QUERY_STRING']
    return url

@application.route('/test')
def test(environ, start_response):
    status = "403 Forbidden"
    headers = [("Content-Type", "text/plain")]
    start_response(status, headers)
    return "bad"

@application.teardown_request
def shutdown_session(exception=None):
    db_session.remove()

"""
Implementation of OAuth for MyProxy Protocol,
https://docs.google.com/document/pub?id=10SC7oSURc-EgxMQjcCS50gz0u2HzDJAFiG5hEHiSdxA
"""

@application.route('/initiate', methods=['GET'])
def initiate(environ, start_response):
  try:
    request = cgi.FieldStorage(fp=environ['wsgi.input'], environ=environ)
    oauth_signature_method = request.getvalue('oauth_signature_method')
    if oauth_signature_method is None:
        oauth_signature_method='RSA-SHA1'
    oauth_signature = str(request.getvalue('oauth_signature'))
    oauth_timestamp = int(request.getvalue('oauth_timestamp'))
    oauth_nonce = int(request.getvalue('oauth_nonce'))
    oauth_version = str(request.getvalue('oauth_version'))
    oauth_consumer_key = str(request.getvalue('oauth_consumer_key'))
    oauth_callback = str(request.getvalue('oauth_callback'))
    certlifetime = request.getvalue('certlifetime')
    if certlifetime is not None:
        certlifetime = int(certlifetime)
    else:
        certlifetime = 86400

    clients = db_session.get_client(Client(oauth_consumer_key=oauth_consumer_key))
    client = None
    if len(clients) > 0:
        client = clients[0]
    if client is None:
        application.logger.error('Unregistered client requested a temporary token.')
        status = "403 Not authorized"
        headers = [
                ("Content-Type", "text/plain") ]
        start_response(status, headers)
        return "Uregistered client"

    if hasattr(Cryptodome.PublicKey.RSA, 'importKey'):
        key = Cryptodome.PublicKey.RSA.importKey(client.oauth_client_pubkey)
    else:
        import M2Crypto.RSA
        import M2Crypto.BIO
        import struct
        import sys

        bio = M2Crypto.BIO.MemoryBuffer(str(client.oauth_client_pubkey))

        k = None
        try:
            k = M2Crypto.RSA.load_pub_key_bio(bio)
            def unpack_from(fmt, data, offs):
                unpack_len = struct.calcsize(fmt)

                return struct.unpack(fmt, data[offs:offs+unpack_len])

            def decode(n):
                len = reduce(lambda x,y: int(x*256+y),
                        unpack_from("4B", n, 0))
                return reduce(lambda x,y: int(x*256+y),
                        unpack_from(str(len)+"B", n, 4))
            keytuple = (decode(k.n), decode(k.e))
        except:
            application.logger.error(str(sys.exc_info()))
            raise

        key = Cryptodome.PublicKey.RSA.construct(keytuple)

    method = environ['REQUEST_METHOD']
    url = url_reconstruct(environ)
    o_request = oauth.Request.from_request(method, url)
    o_consumer = oauth.Consumer(client.oauth_consumer_key, key)
    o_server = oauth.Server()
    o_server.add_signature_method(oauth.SignatureMethod_RSA_SHA1())
    try:
        o_server.verify_request(o_request, o_consumer, None)
    except:
        e = sys.exc_info()
        application.logger.error(str(e[1]))
        status = "403 Not authorized"
        headers = [
                ("Content-Type", "text/plain") ]
        start_response(status, headers, e)
        return str(e[1])

    certreq = str(request.getvalue('certreq'))

    oauth_temp_token = 'myproxy:oa4mp,2012:/tempCred/' \
            + ''.join([random.choice('0123456789abcdef') for i in range(32)]) \
            + '/' + str(int(time.time()))
    transaction = Transaction()
    transaction.temp_token = oauth_temp_token
    transaction.temp_token_valid = 1
    transaction.oauth_callback = oauth_callback
    transaction.certreq = certreq
    transaction.oauth_consumer_key = oauth_consumer_key
    transaction.certlifetime = certlifetime
    transaction.timestamp = int(time.time())
    db_session.add_transaction(transaction)
    db_session.commit()

    status = "200 Ok"
    headers = [
        ("Content-Type", "app/x-www-form-urlencoded") ]
    start_response(status, headers)

    return "oauth_token=%s&oauth_callback_confirmed=true" % oauth_temp_token
  except:
    return bad_request(start_response)


@application.route('/authorize', methods=['GET'])
def get_authorize(environ, start_response):
  try:
    request = cgi.FieldStorage(fp=environ['wsgi.input'], environ=environ)
    oauth_temp_token = str(request.getvalue('oauth_token'))
    transactions = db_session.get_transaction(
            Transaction(temp_token=oauth_temp_token, temp_token_valid=1))
    if len(transactions) == 0:
        status = "403 Not authorized"
        headers = [ ("Content-Type", "text/plain") ]
        start_response(status, headers)
        return 'Invalid temporary token'

    transaction = transactions[0]

    clients = db_session.get_client(Client(oauth_consumer_key=transaction.oauth_consumer_key))
    if len(clients) == 0:
        status = "403 Not authorized"
        headers = [ ("Content-Type", "text/plain") ]
        start_response(status, headers)
        return 'Unregistered client'
    client = clients[0]

    transaction.temp_token_valid = 0
    db_session.update_transaction(transaction)
    db_session.commit()
    styles = ['static/oauth.css']
    css_path = os.path.join(
        os.path.dirname(__file__), 'static', 'site.css')
    if os.path.exists(css_path):
        styles.append("static/site.css")
    res = render_template('authorize.html',
            client_name=client.name,
            client_url=client.home_url,
            temp_token=oauth_temp_token,
            retry_message="",
            stylesheets="\n".join(
                [("<link rel='stylesheet' type='text/css' href='%s' >" % x) for x in styles]))
    status = "200 Ok"
    headers = [ ("Content-Type", "text/html")]
    start_response(status, headers)
    return res
  except:
    return bad_request(start_response)


@application.route('/authorize', methods=['POST'])
def post_authorize(environ, start_response):
  try:
    request = cgi.FieldStorage(fp=environ['wsgi.input'], environ=environ)
    oauth_temp_token = str(request.getvalue('oauth_token'))
    username = str(request.getvalue('username'))
    passphrase = str(request.getvalue('passphrase'))

    transactions = db_session.get_transaction(Transaction(temp_token=oauth_temp_token))
    transaction = None
    if len(transactions) > 0:
        transaction = transactions[0]

    clients = db_session.get_client(Client(oauth_consumer_key=transaction.oauth_consumer_key))
    client = None
    if len(clients) > 0:
        client = clients[0]
    cert = None
    try:
        certreq = "-----BEGIN CERTIFICATE REQUEST-----\n" + str(transaction.certreq) + "-----END CERTIFICATE REQUEST-----\n"
        cert = myproxy.myproxy_logon(certreq,
                transaction.certlifetime,
                username, passphrase, client.myproxy_server)
    except:
        e = sys.exc_info()
        application.logger.error(str(e[1]))
        status = "200 Ok"
        headers = [ ("Content-Type", "text/html") ]
        styles = ['static/oauth.css']
        css_path = os.path.join(
            os.path.dirname(__file__), 'static', 'site.css')
        if os.path.exists(css_path):
            styles.append("static/site.css")
        res = render_template('authorize.html',
                    client_name=client.name,
                    client_url=client.home_url,
                    temp_token=oauth_temp_token,
                    retry_message=str(e[1]),
                    stylesheets="\n".join(
                        [("<link rel='stylesheet' type='text/css' href='%s' >" % x) for x in styles]))
        start_response(status, headers, e)
        return res

    oauth_verifier = 'myproxy:oa4mp,2012:/verifier/' \
            + ''.join([random.choice('0123456789abcdef') for i in range(32)]) \
            + '/' + str(int(time.time()))

    transaction.oauth_verifier = oauth_verifier
    transaction.certificate = cert
    transaction.username = username
    db_session.update_transaction(transaction)
    db_session.commit()

    status = "301 Moved Permanently"
    joiner = "?"
    if "?" in transaction.oauth_callback:
        joiner="&"
    headers = [
            ("Location", str("%s%soauth_token=%s&oauth_verifier=%s" % \
            (transaction.oauth_callback, joiner, oauth_temp_token, oauth_verifier)))]

    start_response(status, headers)
    return ""
  except:
    return bad_request(start_response)

@application.route('/token', methods=['GET'])
def token(environ, start_response):
  try:
    args = cgi.FieldStorage(fp=environ['wsgi.input'], environ=environ)
    oauth_signature_method = args.getvalue('oauth_signature_method')
    if oauth_signature_method is None:
       oauth_signature_method='RSA-SHA1'
    else:
       oauth_signature_method=str(oauth_signature_method)
    oauth_signature = str(args.getvalue('oauth_signature'))
    oauth_timestamp = int(args.getvalue('oauth_timestamp'))
    oauth_nonce = int(args.getvalue('oauth_nonce'))
    oauth_version = str(args.getvalue('oauth_version'))
    oauth_consumer_key = str(args.getvalue('oauth_consumer_key'))
    oauth_temp_token = str(args.getvalue('oauth_token'))
    oauth_verifier = str(args.getvalue('oauth_verifier'))

    oauth_access_token = 'myproxy:oa4mp,2012:/accessToken/' \
            + ''.join([random.choice('0123456789abcdef') for i in range(32)]) \
            + '/' + str(int(time.time()))

    transactions = db_session.get_transaction(Transaction(temp_token=oauth_temp_token))
    transaction = None
    if len(transactions) > 0:
        transaction = transactions[0]
    transaction.access_token = oauth_access_token
    db_session.update_transaction(transaction)
    db_session.commit()

    status = "200 Ok"
    headers = [('Content-Type', 'app/x-www-form-urlencoded')]
    resp = start_response(status, headers)
    return "oauth_token=%s" % str(oauth_access_token)
  except:
    return bad_request(start_response)

@application.route('/getcert', methods=['GET'])
def getcert(environ, start_response):
  try:
    args = cgi.FieldStorage(fp=environ['wsgi.input'], environ=environ)
    oauth_signature_method = args.getvalue('oauth_signature_method')
    if oauth_signature_method is None:
       oauth_signature_method = 'RSA-SHA1'
    else:
       oauth_signature_method = str(oauth_signature_method)
    oauth_signature = str(args.getvalue('oauth_signature'))
    oauth_timestamp = int(args.getvalue('oauth_timestamp'))
    oauth_nonce = int(args.getvalue('oauth_nonce'))
    oauth_version = str(args.getvalue('oauth_version'))
    oauth_consumer_key = str(args.getvalue('oauth_consumer_key'))
    oauth_access_token = str(args.getvalue('oauth_token'))

    transactions = db_session.get_transaction(
            Transaction(access_token=oauth_access_token))
    transaction = None
    if len(transactions) > 0:
        transaction = transactions[0]
    if transaction is None:
        status = "403 Forbidden"
        headers = [("Content-Type", "text/plain")]
        start_response(status, headers)
        return "Invalid access token"

    # Clear database
    old_transactions = [(t) for t in db_session.get_transaction() if t.timestamp < int(time.time())]
    db_session.delete_transactions(old_transactions)
    db_session.commit()

    status = "200 Ok"
    headers = [ ("Content-Type", "app/x-www-form-urlencoded") ]
    start_response(status, headers)

    return 'username=%s\n%s' % (str(transaction.username), str(transaction.certificate))
  except:
    return bad_request(start_response)
# vim: syntax=python: nospell:
