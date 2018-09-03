#!/usr/bin/perl
system('./guc-cc.pl -p 2');
exit($? >> 8);
