#!/usr/bin/perl

use strict;
use POSIX;
use POSIX "sys_wait_h";
use Test::More;

my @tests;

my $server_prog = './gss-assist-auth-accept';
my $client_prog = './gss-assist-auth-init';

my ($valgrind_client, $valgrind_server) = ('', '');
if (exists $ENV{VALGRIND})
{
    $valgrind_client = "valgrind --log-file=VALGRIND-gss_assist_auth_test-client.log";
    $valgrind_server = "valgrind --log-file=VALGRIND-gss_assist_auth_test-server.log";
    if (exists $ENV{VALGRIND_OPTIONS})
    {
        $valgrind_client .= ' ' . $ENV{VALGRIND_OPTIONS};
        $valgrind_server .= ' ' . $ENV{VALGRIND_OPTIONS};
    }
}

sub basic_func
{
    my ($errors,$rc) = ("",0);
    my $expect_failure = shift;
    my $sec_env = shift;
    my $test_name = shift;
    my $result;
    my $server_pid;
    my $client_pid;
    my $port;
    
   if($sec_env == 1)
   {
       $ENV{X509_CERT_DIR} = "/bogus";
   }
   elsif($sec_env == 2)
   {
       $ENV{X509_USER_PROXY} = "/bogus";
   }

   $server_pid = open(SERVER, "$valgrind_server $server_prog |");

   if($server_pid == -1)
   {
       $errors .= "Unable to start server";
       return;
   }

   $port = <SERVER>;
   chomp($port);
   $port =~ s/Socket has port \#//;

   $client_pid = open(CLIENT, "$valgrind_client $client_prog localhost $port|");

   if($client_pid == -1)
   {
       $errors .= "Unable to start client";
       return;
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

   waitpid($client_pid,0);

   if($? != 0)
   {
       $errors .= "Client exited abnormally. \n The following output was generated:\n";
       while(<CLIENT>)
       {
           $errors .= $_;
       }
   }
   
   close(CLIENT);
   close(SERVER);

   print $errors;
   ok(($errors eq "" && !$expect_failure) || $expect_failure, $test_name)
}

push(@tests, "basic_func(0,0, \"auth-with-default-environment\");");
push(@tests, "basic_func(1,1, \"auth-with-bogus-cert-dir\");");
push(@tests, "basic_func(1,2, \"auth-with-bogus-proxy\");");

# Now that the tests are defined, set up the Test to deal with them.
plan tests => scalar(@tests);

# And run them all.
foreach (@tests)
{
   eval "&$_";
}
