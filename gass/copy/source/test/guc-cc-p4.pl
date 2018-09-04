#!/usr/bin/perl
use File::Basename;
system(dirname($0) . '/guc-cc.pl -p 4');
exit($? >> 8);
