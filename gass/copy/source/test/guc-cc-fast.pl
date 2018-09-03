#!/usr/bin/perl
system('./guc-cc.pl -fast');
exit($? >> 8);
