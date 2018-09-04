#!/usr/bin/perl
use File::Basename;
system(dirname($0) . '/guc-cc.pl -p 2');
exit($? >> 8);
