#!/usr/bin/perl
system('./guc-cc.pl -p 4');
exit($? >> 8);
