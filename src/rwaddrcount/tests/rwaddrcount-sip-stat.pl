#! /usr/bin/perl -w
# MD5: 84552210e8ac0fbc9169d495475dc019
# TEST: ./rwaddrcount --print-stat ../../tests/data.rwf

use strict;
use SiLKTests;

my $rwaddrcount = check_silk_app('rwaddrcount');
my %file;
$file{data} = get_data_or_exit77('data');
my $cmd = "$rwaddrcount --print-stat $file{data}";
my $md5 = "84552210e8ac0fbc9169d495475dc019";

check_md5_output($md5, $cmd);
