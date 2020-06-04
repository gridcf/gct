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

=head1 Simple Transfer Tests

Tests to exercise the 3rd party transfer functionality of the Globus
FTP client library.

=cut

use strict;
use Test::More;
use File::Basename;
use lib dirname($0);
use FtpTestLib;
use File::Spec;

my $test_exec = './transfer-test';
my @tests;
my @todo;

=head2 I<basic_func> (Test 1-2)

Do a transfer of test file to/from localhost (with and without a valid proxy).

=over 4

=item Test 1

Transfer file without a valid proxy. Success if test program returns 1,
and no core dump is generated.

=item Test 2

Transfer file with a valid proxy. Success if test program returns 0 and files
compare.

=back

=cut

my ($proto) = setup_proto();
my ($source_host, $source_file, $local_copy) = setup_remote_source();
my ($dest_host, $dest_file) = setup_remote_dest();

sub basic_func
{
    my ($use_proxy) = (shift);
    my ($errors,$rc) = ("",0);

    if($use_proxy == 0)
    {
        FtpTestLib::push_proxy(File::Spec::->devnull());
    }
    
    my $command = "$test_exec -s $proto$source_host$source_file -d $proto$dest_host$dest_file";
    $errors = run_command($command, $use_proxy ? 0 : -1);
    if($use_proxy && $errors eq "")
    {
        my ($output) = get_remote_file($dest_host, $dest_file);
        $errors = compare_local_files($local_copy, $output);
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

=head2 I<bad_url_src> (3-4)

Do a simple transfer of a non-existent source file.

=over 4

=item Test 3

Attempt to transfer a non-existent file. Success if program returns 1
and no core file is generated.

=item Test 4

Attempt to transfer a file from an invalid source port.

=back

=cut
sub bad_url_src
{
    my ($errors,$rc) = ("",0);
    my $src = shift;

    my $command = "$test_exec -s '$src' -d $proto$dest_host$dest_file";
    $errors = run_command($command, 1);

    ok($errors eq "", "bad_url_src $command");
    
    clean_remote_file($dest_host, $dest_file);
}
push(@tests, "bad_url_src('$proto$source_host$source_file/etc/no-such-file-here');");
push(@tests, "bad_url_src('$proto$source_host:4$source_file/etc/no-such-file-here');");

=head2

Do a simple transfer of a non-existent source file.

=over 4

=item Test 5

Attempt to transfer a file to an unwritable location. Success if program
returns 1 and no core file is generated.

=back

=cut
sub bad_url_dest
{
    my ($errors,$rc) = ("",0);

    my $command = "$test_exec -s $proto$source_host$source_file -d $proto$dest_host$dest_file/etc/no-such-file-here";
    $errors = run_command($command, 1);
    
    ok($errors eq "", "bad_url_dest $command");
}
push(@tests, "bad_url_dest();");

=head2 I<abort_test> (Test 6-47)

Do a simple get of $test_url, aborting at each possible state abort
machine. Note that not all aborts will be reached for the "get"
operation.

Success if no core file is generated for all abort points. (we could
use a stronger measure of success here)

=cut
sub abort_test
{
    my ($errors,$rc) = ("", 0);
    my ($abort_point) = shift;

    my $command = "$test_exec -a $abort_point -s $proto$source_host$source_file -d $proto$dest_host$dest_file";
    $errors = run_command($command, -2);

    ok($errors eq "", "abort_test $abort_point $command");
    
    clean_remote_file($dest_host, $dest_file);
}
for(my $i = 1; $i <= 43; $i++)
{
    push(@tests, "abort_test($i);");
}

=head2 I<restart_test> (Test 48-88)

Do a simple transfer of /etc/group to/from localhost, restarting at each
plugin-possible point. Compare the resulting file with the original file.
Success if program returns 0, files compare, and no core file is generated.

=cut
sub restart_test
{
    my ($errors,$rc) = ("",0);
    my ($restart_point) = shift;

    my $command = "$test_exec -r $restart_point -s $proto$source_host$source_file -d $proto$dest_host$dest_file";
    $errors = run_command($command, 0);
    if($errors eq "")
    {
        my ($output) = get_remote_file($dest_host, $dest_file);
        $errors = compare_local_files($local_copy, $output);
        unlink($output);
    }
    
    ok($errors eq "", "restart_test $restart_point $command");
    
    clean_remote_file($dest_host, $dest_file);
}
for(my $i = 1; $i <= 43; $i++)
{
    push(@tests, "restart_test($i);");
}

=head2 I<dcau_test> (Test 89-92)

Do a simple get of /etc/group to/from localhost, using each of the possible
DCAU modes, including subject authorization with a bad subject name.

=over 4

=item Test 89

DCAU with no authorization.

=item Test 90

DCAU with "self" authorization.

=item Test 91

DCAU with subject authorization for our subject name.

=item Test 92

DCAU with subject authorization with an invalid subject.

=back

=cut
sub dcau_test
{
    my ($errors,$rc) = ("",0);
    my ($dcau, $desired_rc) = @_;

    my $command = "$test_exec -c $dcau -s $proto$source_host$source_file -d $proto$dest_host$dest_file";
    $errors = run_command($command, $desired_rc);
    if($errors eq "" && $desired_rc == 0)
    {
        my ($output) = get_remote_file($dest_host, $dest_file);
        $errors = compare_local_files($local_copy, $output);
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

=head2 I<prot_test> (Test 93-95)

Do a simple transfer of /etc/group to/from localhost, with clear, safe, and
private data channel protection.

=over 4

=item Test 93

PROT with clear protection.

=item Test 94

PROT with safe protection.

=item Test 95

PROT with private protection.

=back

=cut
sub prot_test
{
    my ($errors,$rc) = ("",0);
    my ($prot, $desired_rc) = @_;

    my $command = "$test_exec -c self -t $prot -s $proto$source_host$source_file -d $proto$dest_host$dest_file";
    $errors = run_command($command, $desired_rc);
    if($errors eq "" && $desired_rc == 0)
    {
        my ($output) = get_remote_file($dest_host, $dest_file);
        $errors = compare_local_files($local_copy, $output);
        unlink($output);
    }

    ok($errors eq "", "prot_test $command");
    
    clean_remote_file($dest_host, $dest_file);
}

push(@tests, "prot_test('none', 0);");
push(@tests, "prot_test('safe', 0);");
push(@tests, "prot_test('private', 0);");

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
