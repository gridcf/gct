#!/usr/bin/perl
use File::Basename;
system(dirname($0) . '/guc-cc.pl -stripe');
exit($? >> 8);
