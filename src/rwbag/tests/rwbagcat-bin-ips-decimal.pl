#! /usr/bin/perl -w
# MD5: b2c7b5b96aa9cd93221a8e51ef5977f5
# TEST: ./rwbag --sip-flows=stdout ../../tests/data.rwf | ./rwbagcat --key-format=decimal --bin-ips=decimal

use strict;
use SiLKTests;

my $rwbagcat = check_silk_app('rwbagcat');
my $rwbag = check_silk_app('rwbag');
my %file;
$file{data} = get_data_or_exit77('data');
my $cmd = "$rwbag --sip-flows=stdout $file{data} | $rwbagcat --key-format=decimal --bin-ips=decimal";
my $md5 = "b2c7b5b96aa9cd93221a8e51ef5977f5";

check_md5_output($md5, $cmd);
