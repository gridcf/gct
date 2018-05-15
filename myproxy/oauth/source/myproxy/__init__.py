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

#
# myproxy client
#
# Written by Tom Uram <turam@mcs.anl.gov>
# 2005/08/04
# Modified by Lukasz Lacinski <lukasz@ci.uchicago.edu>
# 2012/05/21

import os
import socket
from OpenSSL import crypto, SSL

class GetException(Exception): pass
class RetrieveProxyException(Exception): pass


def deserialize_response(msg):
    """
    Deserialize a MyProxy server response
    Returns: integer response, errortext (if any)
    """

    lines = msg.split('\n')

    # get response value
    responselines = filter( lambda x: x.startswith('RESPONSE'), lines)
    responseline = responselines[0]
    response = int(responseline.split('=')[1])

    # get error text
    errortext = ""
    errorlines = filter( lambda x: x.startswith('ERROR'), lines)
    for e in errorlines:
        etext = e.split('=')[1]
        errortext += etext

    return response, errortext


CMD_GET="""VERSION=MYPROXYv2
COMMAND=0
USERNAME=%s
PASSPHRASE=%s
LIFETIME=%d\0"""

def myproxy_logon(certreq_pem, lifetime, username, passphrase, myproxy_server):
    """
    Function to retrieve a proxy credential from a MyProxy server

    Exceptions:  GetException, RetrieveProxyException
    """
    hostname = myproxy_server
    port = 7512
    try:
        hostname, port = myproxy_server.split(':')
    except:
        pass

    context = SSL.Context(SSL.SSLv23_METHOD)
    context.set_options(SSL.OP_NO_SSLv2)
    context.set_options(SSL.OP_NO_SSLv3)

    # disable for compatibility with myproxy server (er, globus)
    # globus doesn't handle this case, apparently, and instead
    # chokes in proxy delegation code
    context.set_options(0x00000800L)

    # connect to myproxy server
    conn = SSL.Connection(context,socket.socket())
    conn.connect((hostname, int(port)))

    # send globus compatibility stuff
    conn.write('0')

    # send get command
    cmd_get = CMD_GET % (username, passphrase, lifetime)
    conn.write(cmd_get)

    # process server response
    dat = conn.recv(8192)
    response, errortext = deserialize_response(dat)
    if response:
        raise GetException(errortext)

    # generate and send certificate request
    # - The client will generate a public/private key pair and send a 
    #   NULL-terminated PKCS#10 certificate request to the server.
    req = crypto.load_certificate_request(crypto.FILETYPE_PEM, certreq_pem)
    certreq_asn1 = crypto.dump_certificate_request(crypto.FILETYPE_ASN1, req)
    conn.send(certreq_asn1)

    # process certificates
    # - 1 byte, number of certs
    dat = conn.recv(1)
    numcerts = ord(dat[0])

    # - n certs
    dat = conn.recv(8192)

    # process server response
    resp = conn.recv(8192)
    response, errortext = deserialize_response(resp)
    if response:
        raise RetrieveProxyException(errortext)

    # deserialize certs from received cert data and convert them from ASN1 to PEM
    pem_certs = ""

    while dat:
        # find start of cert, get length
        ind = dat.find('\x30\x82')
        if ind < 0:
            break

        len = 256 * ord(dat[ind+2]) + ord(dat[ind+3])

        # extract der-format cert, and convert to pem
        c = dat[ind:ind + len + 4]
        x509 = crypto.load_certificate(crypto.FILETYPE_ASN1, c)
        pem_certs += crypto.dump_certificate(crypto.FILETYPE_PEM, x509)

        # trim cert from data
        dat = dat[ind + len + 4:]

    return pem_certs
