#! /usr/bin/perl

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


=head1 Simple Put Tests

Tests to exercise the "put" functionality of the Globus FTP client library.

=cut

use strict;
use Test::More;
use File::Basename;
use lib dirname($0);
use FtpTestLib;
use File::Spec;

my $test_exec = './put-test';
my @tests;
my @todo;

=pod

=head2 I<basic_func> (Test 1-2)

Do a simple put of $test_url. Compare the resulting file with the real file.

=over 4

=item Test 1

Transfer file without a valid proxy. Success if test program returns 1,
and no core dump is generated.

=item Test 2

Transfer file with a valid proxy. Success if test program returns 0 and
files compare.

=back

=cut

my ($proto) = setup_proto();
my ($dest_host, $dest_file) = setup_remote_dest();
my ($local_copy) = setup_local_source();

sub basic_func
{
    my ($use_proxy) = (shift);
    my ($errors,$rc) = ("",0);

    if($use_proxy == 0)
    {
        FtpTestLib::push_proxy(File::Spec::->devnull());
    }
    my $command = "$test_exec -d $proto$dest_host$dest_file < $local_copy";
    $errors = run_command($command, $use_proxy ? 0 : -1);
    if($use_proxy && $errors eq '')
    {
        my ($output) = get_remote_file($dest_host, $dest_file);
        $errors .= compare_local_files($local_copy, $output);
        unlink($output);
    }

    ok($errors eq "", "basic_func $use_proxy $command");
    if($use_proxy == 0)
    {
        FtpTestLib::pop_proxy();
    }
    
    clean_remote_file($dest_host, $dest_file);
}
push(@tests, "basic_func" . "(0);") unless $proto ne "gsiftp://"; #Use invalid proxy
push(@tests, "basic_func" . "(1);"); #Use proxy


=pod

=head2 I<bad_url> (Test 3)

Do a simple put of a non-existent file.

=over 4

=item Test 3

Attempt to store a file to a bad path. Success if program returns 1
and no core file is generated.

=back

=cut
sub bad_url
{
    my ($errors,$rc) = ("",0);

    my $command = "$test_exec -d $proto$dest_host/no/such/file/here < $local_copy";
    $errors = run_command($command, 1);

    ok($errors eq "", "bad_url $command");
}
push(@tests, "bad_url");

=head2 I<abort_test> (Test 4-44)

Do a simple put of $test_url, aborting at each possible state in the plugin
state machine. Note that all abort states will be reached for the "put"
operation.

Success if no core ifle is generated for all abort points. (We could use
a stronger measure of success here.)

=cut
sub abort_test
{
    my ($errors,$rc) = ("", 0);
    my ($abort_point) = shift;

    my $command = "$test_exec -a $abort_point -d $proto$dest_host$dest_file < $local_copy";
    $errors = run_command($command, -2);

    ok($errors eq "", "abort_test $abort_point $command");
    
    clean_remote_file($dest_host, $dest_file);
}
for(my $i = 1; $i <= 43; $i++)
{
    push(@tests, "abort_test($i);");
}

=head2 I<restart_test> (Test 45-85)

Do a simple put to $test_url, restarting at each plugin-possible
point.  Compare the resulting file with the real file. Success if
program returns 0, files compare, and no core file is generated.

=cut
sub restart_test
{
    my ($errors,$rc) = ("",0);
    my ($restart_point) = shift;

    my $command = "$test_exec -r $restart_point -d $proto$dest_host$dest_file < $local_copy";
    $errors = run_command($command, 0);
    if($errors eq "")
    {
        my ($output) = get_remote_file($dest_host, $dest_file);
        $errors .= compare_local_files($local_copy, $output);
        unlink($output);
    }

    ok($errors eq "", "restart_test $restart_point $command");
    
    clean_remote_file($dest_host, $dest_file);
}
for(my $i = 1; $i <= 43; $i++)
{
    push(@tests, "restart_test($i);");
}
push(@todo, 83);

=head2 I<dcau_test> (Test 86-89)

Do  a simple get of $test_url, using each of the possible DCAU modes,
including subject authorization iwth a bad subject name.

=over 4

=item Test 86

DCAU with no authorization.

=item Test 87

DCAU with "self" authorization.

=item Test 88

DCAU with subject authorization for our subject

=item Test 89

DCAU with subject authorization with an invalid subject.

=back

=cut
sub dcau_test
{
    my ($errors,$rc) = ("",0);
    my ($dcau, $desired_rc) = @_;

    my $command = "$test_exec -c $dcau -d $proto$dest_host$dest_file < $local_copy";
    $errors = run_command($command, $desired_rc);
    if($errors eq "" && $desired_rc == 0)
    {
	my ($output) = get_remote_file($dest_host, $dest_file);
        $errors .= compare_local_files($local_copy, $output);
        unlink($output);
    }

    ok($errors eq "", "dcau_test $command");
    
    clean_remote_file($dest_host, $dest_file);
}

my $subject;

if($ENV{GLOBUS_FTP_CLIENT_TEST_SUBJECT})
{
    $subject = $ENV{GLOBUS_FTP_CLIENT_TEST_SUBJECT};
}
else
{
    $subject = `openssl x509 -in testcred.cert -subject -noout -nameopt sep_multiline | sed -e '/^subject=/d' -e 's!^\\s*!/!' | tr -d '\\n'`;
}

push(@tests, "dcau_test('none', 0);");
push(@tests, "dcau_test('self', 0);");
push(@tests, "dcau_test(\"'$subject'\", 0);");
push(@tests, "dcau_test(\"'/O=Grid/O=Globus/CN=bogus'\", 1);") unless $proto ne "gsiftp://";

=head2 I<prot_test> (Test 90-92)

Do a simple get of $test_url, using DCAU self with clear, safe, and
private data channel protection.

=over 4

=item Test 90

PROT with clear protection

=item Test 91

PROT with safe protection

=item Test 92

PROT with private protection

=back

=cut
sub prot_test
{
    my ($errors,$rc) = ("",0);
    my ($prot, $desired_rc) = @_;

    my $command = "$test_exec -c self -t $prot -d $proto$dest_host$dest_file < $local_copy";
    $errors = run_command($command, $desired_rc);
    if($errors eq "" && $desired_rc == 0)
    {
	my ($output) = get_remote_file($dest_host, $dest_file);
        $errors .= compare_local_files($local_copy, $output);
        unlink($output);
    }

    ok($errors eq "", "prot_test $command");
    
    clean_remote_file($dest_host, $dest_file);
}

push(@tests, "prot_test('clear', 0);");
push(@tests, "prot_test('safe', 0);");
push(@tests, "prot_test('private', 0);");

=head2 I<perf_test> (Test 93)

Do a simple put of /etc/group, enabling perf_plugin

=cut
sub perf_test
{
    my ($errors,$rc) = ("",0);

    my $command = "$test_exec -d $proto$dest_host$dest_file -M < $local_copy";
    $errors = run_command($command, 0);
    
    ok($errors eq "", "perf_test $command");
    
    clean_remote_file($dest_host, $dest_file);
}

push(@tests, "perf_test();");

=head2 I<throughput_test> (Test 94)

Do a simple put of /etc/group, enabling throughput_plugin

=cut
sub throughput_test
{
    my ($errors,$rc) = ("",0);

    my $command = "$test_exec -d $proto$dest_host$dest_file -M -T < $local_copy";
    $errors = run_command($command, 0);
    
    ok($errors eq "", "throughput_test $command");
    
    clean_remote_file($dest_host, $dest_file);
}

push(@tests, "throughput_test();");

=head2 I<restart_plugin_test> (Test 93-?)

Do a get of $test_url, triggering server-side faults, and using
the default restart plugin to cope with them.

=cut
sub restart_plugin_test
{
    my ($errors,$rc) = ("",0);
    my $other_args;

    $ENV{GLOBUS_FTP_CLIENT_FAULT_MODE} = shift;
    $other_args = shift;
    if(!defined($other_args))
    {
	$other_args = "";
    }

    my $command = "$test_exec -d $proto$dest_host$dest_file -f 0,0,0,0 $other_args < $local_copy";
    $errors = run_command($command, 0);
    if($errors eq "")
    {
        my ($output) = get_remote_file($dest_host, $dest_file);
        $errors .= compare_local_files($local_copy, $output);
        unlink($output);
    }
    
    ok($errors eq "", "restart_plugin_test GLOBUS_FTP_CLIENT_FAULT_MODE=$ENV{GLOBUS_FTP_CLIENT_FAULT_MODE} $command");
    
    delete $ENV{GLOBUS_FTP_CLIENT_FAULT_MODE};
    clean_remote_file($dest_host, $dest_file);
}
foreach (&FtpTestLib::ftp_commands())
{
    push(@tests, "restart_plugin_test('$_');");
}
push(@tests, "restart_plugin_test('PROT', '-c self -t safe')");
push(@tests, "restart_plugin_test('DCAU', '-c self -t safe')");
push(@tests, "restart_plugin_test('PBSZ', '-c self -t safe')");

if(defined($ENV{FTP_TEST_RANDOMIZE}))
{
    shuffle(\@tests);
}

if(@ARGV)
{
    plan tests => scalar(@ARGV);

    foreach (@ARGV)
    {
        eval "&$tests[$_-1]";
    }
}
else
{
    plan tests => scalar(@tests), todo => \@todo;

    foreach (@tests)
    {
        eval "&$_";
    }
}
