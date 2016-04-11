#!/usr/bin/env perl

use strict;
use warnings;

use constant MAGIC => 0x561b13d6;

open my $TIME, ">", "store/time";
binmode $TIME;

print $TIME pack "L>CCC" . (4096 - 4 - 1 - 2) . "x", MAGIC, 0, 2, 6;

open my $BID, ">", "store/bid";
binmode $BID;

print $BID pack "L>CCC" . (4096 - 4 - 1 - 2) . "x", MAGIC, 0, 2, 5;

open my $ASK, ">", "store/ask";
binmode $ASK ;

print $ASK pack "L>CCC" . (4096 - 4 - 1 - 2) . "x", MAGIC, 0, 2, 5;

my $dupe = $ENV{DUPE} || 1;

while (<>) {
	chomp;
	my @data = split /,/, $_, 4;
	my @time = split / /, $data[0], 2;

	print $TIME pack "d>", $time[1] * 1000
		foreach (1..$dupe);
	print $BID pack "f>", $data[1]
		foreach (1..$dupe);
	print $ASK pack "f>", $data[2]
		foreach (1..$dupe);
}

close $ASK;
close $BID;
close $TIME;

exit 0;
