#!/usr/bin/perl

# 
# Copyright 1999-2006 University of Chicago
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


=head1 Tests for the globus IO authorization modes

=cut

use strict;
use Test::More;

my @tests;

my $test_prog = 'globus_io_authorization_test';
my $valgrind="";

if (exists $ENV{VALGRIND})
{
    $valgrind = "valgrind --log-file=VALGRIND-$test_prog.log";
    if (exists $ENV{VALGRIND_OPTIONS})
    {
        $valgrind .= ' ' . $ENV{VALGRIND_OPTIONS};
    }
}

sub basic_func
{
    my ($errors,$rc) = ("",0);
    my $args = shift;
    my $expect_failure = shift;
    my $test_name = shift;
   
    diag("$test_prog $args");
    system("$valgrind ./$test_prog $args 1>&2");
    $rc = $?;

    ok((($rc == 0) && !$expect_failure) ||
      (($rc != 0) && $expect_failure),
      $test_name);
}

plan tests => 5;
my $identity = `openssl x509 -subject -in \${X509_USER_CERT-\$HOME/.globus/usercert.pem} -noout -nameopt sep_multiline | sed -e /^subject=/d -e 's!^\\s*!/!' | tr -d '\\n'`;
print "    Using test identity $identity\n";

basic_func('self', 0, "$test_prog-self");
basic_func("identity '$identity'", 0, "$test_prog-identity");
basic_func("identity \"/CN=bad DN\"", 1, "$test_prog-bad-dn");
basic_func("callback", 0, "$test_prog-allowed-callback");
basic_func("-callback", 1, "$test_prog-disallowed-callback");

# And run them all.
foreach (@tests)
{
   eval "&$_";
}
