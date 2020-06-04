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
use POSIX;
use POSIX "sys_wait_h";
use Test::More;

my @tests;

my $test = 'globus-io-tcp-test';
my $server_prog = './globus_io_tcp_test_server';
my $client_prog = './globus_io_tcp_test_client';
my $valgrind="";

if (exists $ENV{VALGRIND})
{
    $valgrind = "valgrind --log-file=VALGRIND-$test-\%p.log";
    if (exists $ENV{VALGRIND_OPTIONS})
    {
        $valgrind .= ' ' . $ENV{VALGRIND_OPTIONS};
    }
}

my $identity = `openssl x509 -subject -in \${X509_USER_CERT-\$HOME/.globus/usercert.pem} -noout -nameopt sep_multiline | sed -e '/^subject=/d' -e 's!^\\s*!/!' | tr -d '\\n'`;
diag("Using test identity $identity");

sub basic_func
{
   my ($errors,$rc) = ("",0);
   my $server_args = shift;
   my $client_args = shift;
   my $expect_failure = shift;
   my $sec_env = shift;
   my $test_name = shift;
   my $result;
   my $server_pid;
   my $client_pid;
   my $port;
   my $saved_cert_dir;
   my $saved_user_cert;
   my $saved_user_key;
   my $saved_user_proxy;

   if(defined($ENV{X509_CERT_DIR}))
   {
       $saved_cert_dir = $ENV{X509_CERT_DIR};
   }
   if(defined($ENV{X509_USER_CERT}))
   {
       $saved_user_cert = $ENV{X509_USER_CERT};
   }
   if(defined($ENV{X509_USER_KEY}))
   {
       $saved_user_key = $ENV{X509_USER_KEY};
   }
   if(defined($ENV{X509_USER_PROXY}))
   {
       $saved_user_proxy = $ENV{X509_USER_PROXY};
   }

   if($sec_env == 0)
   {
       # use existing (good) gsi env
   }
   elsif($sec_env == 1)
   {
       # break gsi env
       $ENV{X509_CERT_DIR} = "/bogus";
   }
   elsif($sec_env == 2)
   {
       # break gsi env
       $ENV{X509_USER_CERT} = "/bogus";
       $ENV{X509_USER_KEY} = "/bogus";
       $ENV{X509_USER_PROXY} = "/bogus";
   }
   
   my $command = "$valgrind $server_prog $server_args |";
   diag("Running server: $command");
   $server_pid = open(SERVER, $command);

   if($server_pid == -1)
   {
       $errors .= "Unable to start server";
       goto end;
   }

   $port = <SERVER>;
   diag("Server said $port");
   chomp($port);
   $port =~ s/listening on port //;

   sleep(2);

   $command = "$valgrind $client_prog -h localhost -p $port $client_args |";
   diag("Running client: $command");
   $client_pid = open(CLIENT, $command);

   if($client_pid == -1)
   {
       $errors .= "Unable to start client";
       goto end;
   }
   
   waitpid($client_pid,0);
   my $client_exit_code = $? >> 8;

   if($client_exit_code != 0)
   {
       $errors .= "Client exited abnormally $client_exit_code. \n The following output was generated:\n";
       while(<CLIENT>)
       {
           $errors .= $_;
       }
       kill(9, $server_pid);
   }
   
   waitpid($server_pid,0);

   if($? != 0)
   {
       $errors .= "Server exited abnormally. \n The following output was generated:\n";
       while(<SERVER>)
       {
           $errors .= $_;
       }
   }

   close(CLIENT);
   close(SERVER);

   diag($errors);
   ok(($errors eq "" && !$expect_failure) || ($errors ne "" && $expect_failure),
        $test_name);

end:

   if(defined($saved_cert_dir)) 
   {
       $ENV{X509_CERT_DIR} = $saved_cert_dir;
   }
   elsif(defined($ENV{X509_CERT_DIR}))
   {
       delete $ENV{X509_CERT_DIR};
   }
   if(defined($saved_user_cert)) 
   {
       $ENV{X509_USER_CERT} = $saved_user_cert;
   }
   elsif(defined($ENV{X509_USER_CERT}))
   {
       delete $ENV{X509_USER_CERT};
   }
   if(defined($saved_user_key)) 
   {
       $ENV{X509_USER_KEY} = $saved_user_key;
   }
   elsif(defined($ENV{X509_USER_KEY}))
   {
       delete $ENV{X509_USER_KEY};
   }
   if(defined($saved_user_proxy)) 
   {
       $ENV{X509_USER_PROXY} = $saved_user_proxy;
   }
   elsif(defined($ENV{X509_USER_PROXY}))
   {
       delete $ENV{X509_USER_PROXY};
   }
}



push(@tests, "basic_func(\"\",\"\",0,0,'$test-default');");
push(@tests, "basic_func(\"\",\"\",1,1,'$test-bad-cert-dir');");
push(@tests, "basic_func(\"\",\"\",1,2,'$test-bad-creds');");
push(@tests, "basic_func(\"-g\",\"-g\",0,0, '$test-gsi-wrap');");
push(@tests, "basic_func(\"-g\",\"-g\",1,1, '$test-gsi-wrap-bad-cert-dir');");
push(@tests, "basic_func(\"-g\",\"-g\",1,2, '$test-gsi-wrap-bad-creds');");
push(@tests, "basic_func(\"\",\"-g\",1,0, '$test-gsi-wrap-client-only');");
push(@tests, "basic_func(\"-s\",\"-s\",0,0, '$test-ssl-wrap');");
push(@tests, "basic_func(\"-s\",\"-s\",1,1, '$test-ssl-wrap-bad-cert-dir');");
push(@tests, "basic_func(\"-s\",\"-s\",1,2, '$test-ssl-wrap-bad-creds');");
push(@tests, "basic_func(\"-s\",\"\",1,0, '$test-ssl-wrap-server-only');");
push(@tests, "basic_func(\"\",\"-s\",1,0, '$test-ssl-wrap-client-only');");
push(@tests, "basic_func(\"-c\",\"-c\",0,0, '$test-clear');");
push(@tests, "basic_func(\"-c\",\"-c\",1,1, '$test-clear-bad-cert-dir');");
push(@tests, "basic_func(\"-c\",\"-c\",1,2, '$test-clear-bad-creds');");
push(@tests, "basic_func(\"-c\",\"\",0,0, '$test-clear-server-only');");
push(@tests, "basic_func(\"\",\"-c\",0,0, '$test-clear-client-only');");
push(@tests, "basic_func(\"\",\"-H\",1,0, '$test-client-bad-host-authz');");
push(@tests, "basic_func(\"-g\",\"-H -g\",1,0, '$test-gsi-wrap-bad-host-authz');");
push(@tests, "basic_func(\"-s\",\"-H -s\",1,0, '$test-ssl-wrap-bad-host-authz');");
push(@tests, "basic_func(\"-c\",\"-H -c\",1,0, '$test-ssl-wrap-bad-host-authz');");
push(@tests, "basic_func(\"-c\",\"-i \'$identity\' -c\",0,0, '$test-clear-client-identity-authz');");
push(@tests, "basic_func(\"-i \'$identity\' -c\",\"-c\",0,0, '$test-clear-server-identity-authz');");
push(@tests, "basic_func(\"-c\",\"-i \'/CN=bogus\' -c\",1,0, '$test-clear-client-bad-identity-authz');");
push(@tests, "basic_func(\"-i \'/CN=bogus\' -c\",\"-c\",1,0, '$test-clear-server-bad-identity-authz');");
push(@tests, "basic_func(\"\",\"-d\",0,0, '$test-delegate-limited');");
push(@tests, "basic_func(\"\",\"-D\",0,0, '$test-delegate-full');");
push(@tests, "basic_func(\"-g\",\"-d -g\",0,0, '$test-delgate-limited-gsi-wrap');");
push(@tests, "basic_func(\"-g\",\"-D -g\",0,0, '$test-delegate-full-gsi-wrap');");
push(@tests, "basic_func(\"-c\",\"-d -c\",0,0, '$test-delegate-limited-clear');");
push(@tests, "basic_func(\"-c\",\"-D -c\",0,0, '$test-delegate-full-clear');");
push(@tests, "basic_func(\"-v\",\"-v\",0,0, '$test-iovec');");
push(@tests, "basic_func(\"-v -g\",\"-v -g\",0,0, '$test-iovec-gsi-wrap');");
push(@tests, "basic_func(\"-v -s\",\"-v -s\",0,0, '$test-iovec-ssl-wrap');");
push(@tests, "basic_func(\"-v -c\",\"-v -c\",0,0, '$test-iovec-clear');");
push(@tests, "basic_func(\"-i \'<anonymous>\'\",\"-a -i \'$identity\'\",0,0, '$test-anonymous');");
push(@tests, "basic_func(\"\",\"-a -i \'$identity\'\",1,0, '$test-anonymous-identity');");
# Disabled until "RIC-238: GSI XIO Driver hangs in delegation code" is fixed
#push(@tests, "basic_func(\"-b\",\"-b\",0,0, '$test-io-delegation');");
#push(@tests, "basic_func(\"-b -g\",\"-b -g\",0,0, '$test-io-delegation-gsi-wrap');");
#push(@tests, "basic_func(\"-b -s\",\"-b -s\",0,0, '$test-io-delegation-ssl-wrap');");
#push(@tests, "basic_func(\"-b -c\",\"-b -c\",0,0, '$test-io-delegation-clear');");
push(@tests, "basic_func(\"-g -P none\",\"-g -P none\",0,0,'$test-gsi-wrap-noprotect');");
push(@tests, "basic_func(\"-g -P integrity\",\"-g -P integrity\",0,0,'$test-gsi-wrap-integrity');");
push(@tests, "basic_func(\"-g -P privacy\",\"-g -P privacy\",0,0, '$test-gsi-wrap-privacy');");

# Now that the tests are defined, set up the Test to deal with them.
plan tests => scalar(@tests);

# And run them all.
foreach (@tests)
{
   eval "&$_";
}
