#!/usr/bin/perl
system('./guc-cc.pl -stripe -p 4');
exit($? >> 8);
