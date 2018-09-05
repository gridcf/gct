#! /usr/bin/perl
# Copyright 1999-2016 University of Chicago
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

use strict;
use warnings;

use Fcntl ':mode';
use File::Path;
use File::Temp ':POSIX';
use Test::More;

my $system_config_test = "./system-config-test";

sub set_key_permissions {
    my $umask = umask(0);
    my $filename = tmpnam();
    my ($output, $rc, $mode);

    $output = qx($system_config_test set_key_permissions $filename);
    $rc = $? >> 8;

    diag($output) if $output ne "";

    $mode = (stat($filename))[2] & (S_IRWXU|S_IRWXG|S_IRWXO);
    diag(sprintf "mode of new file is %o", $mode);

    ok($rc == 0 && ($mode & (S_IRWXG|S_IRWXO)) == 0, "set_key_permissions");
    unlink($filename);
    umask($umask);
}

sub get_home_dir {
    my $homedir = (getpwuid($<))[7];

    SKIP: {
        skip "User home dir doesn't exist", 1 unless -d $homedir;

        my ($rc, $output);

        $output = qx($system_config_test get_home_dir);
        $rc = $? >> 8;

        $output =~ s/\n$//;

        diag($output) if $output ne "";

        ok($homedir eq $output, "get_home_dir");
    }
}

sub file_exists_true {
    my ($fh, $filename) = File::Temp::tempfile();
    my ($rc, $output);

    print $fh "Hello\n";
    $fh->flush();

    $output = qx($system_config_test file_exists $filename);
    $rc = $? >> 8;

    ok($rc == 0, "file_exists_true");
}

sub file_exists_false {
    my ($fh, $filename) = File::Temp::tempfile();
    my ($rc, $output);

    close($fh);
    unlink($filename);

    $output = qx($system_config_test file_exists $filename 2>&1);
    $rc = $? >> 8;

    chomp($output);
    diag("Expect error message: $output");

    ok($rc != 0, "file_exists_false");
}

sub dir_exists_true {
    my ($dir) = File::Temp::tempdir(CLEANUP => 1);
    my ($rc, $output);


    $output = qx($system_config_test dir_exists $dir);
    $rc = $? >> 8;

    ok($rc == 0, "dir_exists_true");
}

sub dir_exists_false {
    my ($dir) = File::Temp::tempdir();
    my ($rc, $output);

    rmdir $dir;

    $output = qx($system_config_test dir_exists $dir 2>&1);
    $rc = $? >> 8;

    chomp($output);
    diag("Expect error message: $output");

    ok($rc != 0, "dir_exists_false");
}

sub check_keyfile_true {
    my $umask = umask(0);
    my $filename = tmpnam();
    my ($output, $rc, $mode, $fh);

    $output = qx($system_config_test set_key_permissions $filename);
    $rc = $? >> 8;

    diag($output) if $output ne "";
    $mode = (stat($filename))[2] & (S_IRWXU|S_IRWXG|S_IRWXO);
    diag(sprintf "mode of new file is %o", $mode);

    open($fh, ">$filename");
    print $fh "Hello\n";
    close($fh);

    $output = qx($system_config_test check_keyfile $filename);
    $rc = $? >> 8;

    ok($rc == 0, "check_keyfile_true");
    unlink($filename);
    umask($umask);
}

sub check_keyfile_false {
    my $umask = umask(0);
    my ($fh, $filename) = File::Temp::tempfile();
    my ($output, $rc, $mode);

    chmod 0644, $filename;
    print $fh "Hello\n";
    $fh->flush();

    $output = qx($system_config_test check_keyfile $filename 2>&1);
    $rc = $? >> 8;

    chomp($output);
    diag("Expect error: $output");

    $mode = (stat($filename))[2] & (S_IRWXU|S_IRWXG|S_IRWXO);

    ok($rc != 0 && ($mode & (S_IRWXG|S_IRWXO)) != 0, "check_keyfile_false");
    unlink($filename);
    umask($umask);
}

sub check_certfile_true {
    my $umask = umask(0);
    my $filename = tmpnam();
    my ($output, $rc, $mode, $fh);

    $output = qx($system_config_test set_key_permissions $filename);
    $rc = $? >> 8;

    diag($output) if $output ne "";
    $mode = (stat($filename))[2] & (S_IRWXU|S_IRWXG|S_IRWXO);
    diag(sprintf "mode of new file is %o", $mode);

    open($fh, ">$filename");
    print $fh "Hello\n";
    close($fh);

    $output = qx($system_config_test check_certfile $filename);
    $rc = $? >> 8;

    ok($rc == 0, "check_certfile_true");
    unlink($filename);
    umask($umask);
}

sub check_certfile_false {
    my $umask = umask(0);
    my ($fh, $filename) = File::Temp::tempfile();
    my ($output, $rc, $mode);

    chmod 0664, $filename;
    print $fh "Hello\n";
    $fh->flush();

    $output = qx($system_config_test check_certfile $filename 2>&1);
    $rc = $? >> 8;

    chomp($output);
    diag("Expect error: $output");

    $mode = (stat($filename))[2] & (S_IRWXU|S_IRWXG|S_IRWXO);

    unlink($filename);
    umask($umask);

    ok($rc != 0 && ($mode & (S_IRWXG|S_IRWXO)) != 0, "check_certfile_false");
}

sub get_cert_dir_env {
    my ($dirname) = File::Temp::tempdir(CLEANUP => 1);
    my ($output, $rc, $mode);
    my $old_dir = $ENV{X509_CERT_DIR};
    
    $ENV{X509_CERT_DIR} = $dirname;
    diag("X509_CERT_DIR=$dirname");

    $output = qx($system_config_test get_cert_dir);
    $rc = $? >> 8;

    chomp($output);
    diag($output) if $output ne "";

    if ($old_dir)
    {
        $ENV{X509_CERT_DIR} = $old_dir;
    }
    else
    {
        delete $ENV{X509_CERT_DIR};
    }
    ok($dirname eq $output, "get_cert_dir_env");
}

sub get_cert_dir_env_format {
    my ($dirname) = File::Temp::tempdir(CLEANUP => 1);
    my $format_dirname = "$dirname/\%p";
    my ($output, $rc, $mode);
    my $old_dir = $ENV{X509_CERT_DIR};

    mkdir "$format_dirname";
    
    $ENV{X509_CERT_DIR} = $format_dirname;
    diag("X509_CERT_DIR=$format_dirname");

    $output = qx($system_config_test get_cert_dir);
    $rc = $? >> 8;

    chomp($output);
    diag($output) if $output ne "";

    if ($old_dir)
    {
        $ENV{X509_CERT_DIR} = $old_dir;
    }
    else
    {
        delete $ENV{X509_CERT_DIR};
    }
    ok($rc == 0 && $format_dirname eq $output, "get_cert_dir_env_format");
}

sub get_cert_dir_env_bad {
    my ($dirname) = File::Temp::tempdir(CLEANUP => 0);
    my ($output, $rc, $mode);
    my $old_dir = $ENV{X509_CERT_DIR};

    rmdir $dirname;
    
    $ENV{X509_CERT_DIR} = $dirname;
    diag("X509_CERT_DIR=$dirname");

    $output = qx($system_config_test get_cert_dir 1>&2);
    $rc = $? >> 8;

    chomp($output);
    diag($output) if $output ne "";

    if ($old_dir)
    {
        $ENV{X509_CERT_DIR} = $old_dir;
    }
    else
    {
        delete $ENV{X509_CERT_DIR};
    }
    ok($rc != 0, "get_cert_dir_env_bad");
}


sub get_cert_dir_home {
    my $homedir = (getpwuid($<))[7];
    my $certdir = "$homedir/.globus/certificates";
    my $old_dir = $ENV{X509_CERT_DIR};
    my ($rc, $output);
    
    SKIP: {
        skip "User cert dir doesn't exist", 1 unless -d $certdir;

        delete $ENV{X509_CERT_DIR} if $old_dir;

        $output = qx($system_config_test get_cert_dir);
        $rc = $? >> 8;

        chomp($output);
        diag($output) if $output ne "";

        if ($old_dir) {
            $ENV{X509_CERT_DIR} = $old_dir;
        }

        ok($rc == 0 && $output eq $certdir, "get_cert_dir_home");
    }
}

sub get_user_cert_filename_pem {
    my $homedir = (getpwuid($<))[7];
    my $certfile = "$homedir/.globus/usercert.pem";
    my $keyfile = "$homedir/.globus/userkey.pem";
    my ($rc, $output);
    my ($testcertfile, $testkeyfile);
    my $old_cert_env = $ENV{X509_USER_CERT};
    my $old_key_env = $ENV{X509_USER_KEY};

    SKIP: {
        skip "Default PEM-formatted cert and key don't exist", 1
            unless -r $certfile && -r $keyfile;

        delete $ENV{X509_USER_CERT} if $old_cert_env;
        delete $ENV{X509_USER_KEY} if $old_key_env;
    
        $output = qx($system_config_test get_user_cert_filename);
        $rc = $? >> 8;

        chomp($output);
        diag($output) if $output ne "";

        ($testcertfile, $testkeyfile) = split(/\n/, $output, 2);

        ok($rc == 0 && $testcertfile eq $certfile && $testkeyfile eq $keyfile,
           "get_user_cert_filename_pem");

        if ($old_cert_env) {
            $ENV{X509_USER_CERT} = $old_cert_env 
        }
        if ($old_key_env) {
            $ENV{X509_USER_KEY} = $old_key_env 
        }
    }
}

sub get_user_cert_filename_p12 {
    my $homedir = (getpwuid($<))[7];
    my $p12file = "$homedir/.globus/usercred.p12";
    my ($rc, $output);
    my ($testcertfile, $testkeyfile);
    my $old_cert_env = $ENV{X509_USER_CERT};
    my $old_key_env = $ENV{X509_USER_KEY};
    
    SKIP: {
        skip "Default PKCS12-formatted cert and key don't exist", 1
            unless -r $p12file;

        delete $ENV{X509_USER_CERT} if $old_cert_env;
        delete $ENV{X509_USER_KEY} if $old_key_env;
    
        $output = qx($system_config_test get_user_cert_filename);
        $rc = $? >> 8;

        chomp($output);
        diag($output) if $output ne "";

        ($testcertfile, $testkeyfile) = split(/\n/, $output, 2);
        diag("testcertfile='$testcertfile'");
        diag("testkeyfile='$testkeyfile'");

        ok($rc == 0 && $testcertfile eq $p12file && $testkeyfile eq $p12file,
            "get_user_cert_filename_p12");
        if ($old_cert_env) {
            $ENV{X509_USER_CERT} = $old_cert_env 
        }
        if ($old_key_env) {
            $ENV{X509_USER_KEY} = $old_key_env 
        }
    }
}

sub get_user_cert_filename_env {
    my ($certfh, $certfile) = File::Temp::tempfile();
    my ($keyfh, $keyfile) = File::Temp::tempfile();
    my ($rc, $output);
    my ($testcertfile, $testkeyfile);
    my $old_cert_env = $ENV{X509_USER_CERT};
    my $old_key_env = $ENV{X509_USER_KEY};

    delete $ENV{X509_USER_CERT};
    delete $ENV{X509_USER_KEY};

    print $certfh "Hello\n";
    $certfh->flush();

    print $keyfh "Hello\n";
    $keyfh->flush();

    $ENV{X509_USER_CERT} = $certfile;
    $ENV{X509_USER_KEY} = $keyfile;


    $output = qx($system_config_test get_user_cert_filename);
    $rc = $? >> 8;

    chomp($output);
    diag($output) if $output ne "";

    ($testcertfile, $testkeyfile) = split(/\n/, $output, 2);
    diag("testcertfile='$testcertfile'");
    diag("testkeyfile='$testkeyfile'");

    ok($rc == 0 && $testcertfile eq $certfile && $testkeyfile eq $keyfile,
            "get_user_cert_filename_env");

    if ($old_cert_env)
    {
        $ENV{X509_USER_CERT} = $old_cert_env;
    }
    else
    {
        delete $ENV{X509_USER_CERT};
    }
    if ($old_key_env)
    {
        $ENV{X509_USER_KEY} = $old_key_env;
    }
    else
    {
        delete $ENV{X509_USER_KEY};
    }
}

sub get_user_cert_filename_env_format {
    my $testdir = File::Temp::tempdir(CLEANUP => 1);
    my ($certfile) = "$testdir/cert%p";
    my ($keyfile) = "$testdir/key%p";
    my ($certfh, $keyfh);
    my ($rc, $output);
    my ($testcertfile, $testkeyfile);
    my $old_cert_env = $ENV{X509_USER_CERT};
    my $old_key_env = $ENV{X509_USER_KEY};

    delete $ENV{X509_USER_CERT};
    delete $ENV{X509_USER_KEY};

    $ENV{X509_USER_CERT} = $certfile;
    $ENV{X509_USER_KEY} = $keyfile;

    open $certfh, ">$certfile";
    print $certfh "Hello\n";
    close($certfh);
    chmod 0644, $certfile;

    open $keyfh, ">$keyfile";
    print $keyfh "Hello\n";
    close($keyfh);
    chmod 0600, $keyfile;

    $output = qx($system_config_test get_user_cert_filename);
    $rc = $? >> 8;

    chomp($output);
    diag($output) if $output ne "";

    ($testcertfile, $testkeyfile) = split(/\n/, $output, 2);
    diag("testcertfile='$testcertfile'");
    diag("testkeyfile='$testkeyfile'");

    ok($rc == 0 && $testcertfile eq $certfile && $testkeyfile eq $keyfile,
            "get_user_cert_filename_env_format");

    if ($old_cert_env)
    {
        $ENV{X509_USER_CERT} = $old_cert_env;
    }
    else
    {
        delete $ENV{X509_USER_CERT};
    }
    if ($old_key_env)
    {
        $ENV{X509_USER_KEY} = $old_key_env;
    }
    else
    {
        delete $ENV{X509_USER_KEY};
    }
}

sub get_user_cert_filename_env_bad {
    my $testdir = File::Temp::tempdir(CLEANUP => 1);
    my ($certfile) = "$testdir/cert";
    my ($keyfile) = "$testdir/key";
    my ($rc, $output);
    my $old_cert_env = $ENV{X509_USER_CERT};
    my $old_key_env = $ENV{X509_USER_KEY};

    delete $ENV{X509_USER_CERT};
    delete $ENV{X509_USER_KEY};

    $ENV{X509_USER_CERT} = $certfile;
    $ENV{X509_USER_KEY} = $keyfile;

    $output = qx($system_config_test get_user_cert_filename 2>&1);
    $rc = $? >> 8;

    chomp($output);
    diag($output) if $output ne "";

    ok($rc != 0, "get_user_cert_filename_env_bad");

    if ($old_cert_env)
    {
        $ENV{X509_USER_CERT} = $old_cert_env;
    }
    else
    {
        delete $ENV{X509_USER_CERT};
    }
    if ($old_key_env)
    {
        $ENV{X509_USER_KEY} = $old_key_env;
    }
    else
    {
        delete $ENV{X509_USER_KEY};
    }
}

sub get_vhost_cred_dir_globus_location
{
    my ($dirname) = File::Temp::tempdir(CLEANUP => 1);
    my ($output, $rc, $mode);
    my $old_dir = $ENV{X509_VHOST_CRED_DIR};
    my $old_globus_location = $ENV{GLOBUS_LOCATION};
    my $vhostdir = "$dirname/etc/vhosts";

    File::Path::make_path($vhostdir);

    delete $ENV{X509_VHOST_CRED_DIR};
    $ENV{GLOBUS_LOCATION} = $dirname;
    diag("GLOBUS_LOCATION=$dirname");

    $output = qx($system_config_test get_vhost_cred_dir);
    $rc = $? >> 8;

    chomp($output);
    diag($output) if $output ne "";

    if ($old_dir)
    {
        $ENV{X509_VHOST_CRED_DIR} = $old_dir;
    }
    else
    {
        delete $ENV{X509_VHOST_CRED_DIR};
    }

    if ($old_dir) {
        $ENV{X509_VHOST_CRED_DIR} = $old_dir;
    }
    if ($old_globus_location) {
        $ENV{GLOBUS_LOCATION} = $old_globus_location;
    } else {
        delete $ENV{GLOBUS_LOCATION};
    }

    ok($rc == 0 && $output eq $vhostdir, "get_vhost_cred_dir_globus_location");
}

sub get_vhost_cred_dir_env
{
    my ($dirname) = File::Temp::tempdir(CLEANUP => 1);
    my ($output, $rc, $mode);
    my $old_dir = $ENV{X509_VHOST_CRED_DIR};
    
    $ENV{X509_VHOST_CRED_DIR} = $dirname;
    diag("X509_VHOST_CRED_DIR=$dirname");

    $output = qx($system_config_test get_vhost_cred_dir);
    $rc = $? >> 8;

    chomp($output);
    diag($output) if $output ne "";

    if ($old_dir)
    {
        $ENV{X509_VHOST_CRED_DIR} = $old_dir;
    }
    else
    {
        delete $ENV{X509_VHOST_CRED_DIR};
    }
    ok($dirname eq $output, "get_vhost_cred_dir");
}

my @tests = qw (
    set_key_permissions
    get_home_dir
    file_exists_true
    file_exists_false
    dir_exists_true
    dir_exists_false
    check_keyfile_true
    check_keyfile_false
    check_certfile_true
    check_certfile_false
    get_cert_dir_env
    get_cert_dir_env_format
    get_cert_dir_env_bad
    get_cert_dir_home
    get_user_cert_filename_pem
    get_user_cert_filename_p12
    get_user_cert_filename_env
    get_user_cert_filename_env_format
    get_user_cert_filename_env_bad
    get_vhost_cred_dir_globus_location
    get_vhost_cred_dir_env
);

plan tests => scalar(@tests);

foreach (@tests) {
    eval $_;
    if ($@) {
        print STDERR $@;
    }
}
