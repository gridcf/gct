#! /usr/bin/perl

# 
# Copyright 1999-2014 University of Chicago
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

# These tests use globus-url-copy with various striping and concurrency options
require 5.8.0;

use strict;
use warnings;
use Test::More;
use Cwd;
use IPC::Open3;
use URI;
use Symbol qw/gensym/;
use Getopt::Long;
use File::Temp qw/ tempfile tempdir /;
use File::Copy;
use File::Path qw/rmtree/;

my $subject = $ENV{FTP_TEST_SUBJECT};
my $server_cs = $ENV{FTP_TEST_CONTACT};
my $path_transform_with_cygpath_w = $ENV{CYGPATH_W_DEFINED};

my @mode = @ARGV;

my $dc_opt = [];
$dc_opt = ["-subject", $subject] if $subject;

my @concur;
push(@concur, []);
my $concur_max=20;
for(my $i = 1; $i < $concur_max; $i += 3)
{
    push(@concur, ["-cc", $i]);
}

my $work_dir = tempdir( CLEANUP => 1);
mkdir("$work_dir/GL");

my @chars=("A".."Z","a".."z","0".."9");
for (my $i = 0; $i < 64; $i++)
{
    my $fn = "";
    $fn .= $chars[rand(@chars)] for 1..8;
    my $fd;
    open($fd, ">$work_dir/$fn");
    print $fd $chars[rand @chars] for 1..int(rand(4096));
}

mkdir("$work_dir/empty", 0700);
my $fd;
open($fd, ">$work_dir/file_mt") && close($fd);

open ($fd, ">$work_dir/src.data");
print $fd $chars[rand @chars] for 1..10240;

for(my $i = 1; $i <= 10; $i++)
{
    mkdir("$work_dir/GL/$i", 0700);
    for(my $j = 1; $j <= $i; $j++)
    {
        copy("$work_dir/src.data", "$work_dir/GL/$i/$j");
    }
}

sub transform_path
{
    my $in = shift;
    my $out = $in;

    if ($path_transform_with_cygpath_w)
    {
        if ($in =~ m/^\S+:/) {
            my $inurl = URI->new($in);
            my $cygpath_cmd;
            $cygpath_cmd = "cygpath -m " . $inurl->path;
            $out = `$cygpath_cmd`;
            $out =~ s/\s*$//;
            $out =~ s/://;
            $inurl->path($out);
            $out = $inurl->as_string;
        } else {
            $out = `cygpath -w $out`;
            $out =~ s/\s*$//;
        }
    }
    return $out;
}

my $test_count = 2*scalar(@concur);
plan tests => $test_count;

SKIP: {
    skip "Missing URL or subject", $test_count unless($server_cs && $subject);
    {
        my $p=$_;
        my $server_port = $server_cs;
        $server_port =~ s/.*://;
        my $dst_url = transform_path("${server_cs}${work_dir}/GL2/");
        my $src_url = transform_path("${server_cs}${work_dir}/GL/");

        foreach my $cc (@concur)
        {
            my ($infd, $outfd, $errfd);
            my ($out, $err);
            my ($pid, $rc);
            my @args = ("globus-url-copy-noinst",
                @mode, @{$cc}, @{$dc_opt},
                "-cd", "-r", $src_url, $dst_url);
            $errfd = gensym;

            print STDERR "# Executing " . join (" ", @args) . "\n";
            $pid = open3($infd, $outfd, $errfd, @args);
            close($infd);

            waitpid($pid, 0);
            $rc = $?;

            {
                local($/);
                $out = <$outfd> if $outfd;
                $err = <$errfd> if $errfd;

                $out =~ s/^/# /mg if $out;
                $err =~ s/^/# /mg if $err;

                print STDERR "# stdout:\n$out" if $out;
                print STDERR "# stderr:\n$err" if $err;
            }

            ok($rc == 0, join(" ", "guc cc", @mode, @{$cc},
                ,"exits with 0"));

            $errfd = gensym;
            $pid = open3($infd, $outfd, $errfd, "diff", "-r",
                "$work_dir/GL", "$work_dir/GL2");
            close($infd);
            waitpid($pid, 0);
            $rc = $?;
            {
                local($/);
                $out = <$outfd> if $outfd;
                $err = <$errfd> if $errfd;

                $out =~ s/^/# /mg if $out;
                $err =~ s/^/# /mg if $err;

                print STDERR "# stdout:\n$out" if $out;
                print STDERR "# stderr:\n$err" if $err;
            }

            ok($rc == 0, join(" ", "guc cc diff ", @mode, @{$cc}));
            rmtree("$work_dir/GL2");
        }
    }
}
exit(77) if ((!$server_cs) || (!$subject));
