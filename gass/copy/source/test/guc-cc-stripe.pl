#!/usr/bin/perl
system('./guc-cc.pl -stripe');
exit($? >> 8);
