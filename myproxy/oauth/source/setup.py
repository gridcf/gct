#! /usr/bin/python

from distutils.core import setup

setup(name = 'myproxy_oauth',
    version = '0.27',
    description = 'MyProxy OAuth Delegation Service',
    author = 'Globus Toolkit',
    author_email = 'support@globus.org',
    packages = [
            'myproxy',
            'myproxyoauth',
            'myproxyoauth.templates',
            'myproxyoauth.static',
            'oauth2'],
    package_data = {
        'myproxyoauth': [ 'templates/*.html', 'static/*.png', 'static/*.css' ]
    },
    scripts = [ 'wsgi.py', 'myproxy-oauth-setup' ],
    data_files = [
            ('apache', [
                    'conf/myproxy-oauth',
                    'conf/myproxy-oauth-2.4',
                    'conf/myproxy-oauth-2.4-deb',
                    'conf/myproxy-oauth-epel5' ])]
)
