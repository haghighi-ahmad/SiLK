#! /usr/bin/perl -w
# MD5: 34122422586fe1a3d58f4b5355dbec6b
# TEST: ./rwbag --sip-flows=stdout ../../tests/data.rwf | ./rwbagcat --key-format=decimal --bin-ips=binary

use strict;
use SiLKTests;

my $rwbagcat = check_silk_app('rwbagcat');
my $rwbag = check_silk_app('rwbag');
my %file;
$file{data} = get_data_or_exit77('data');
my $cmd = "$rwbag --sip-flows=stdout $file{data} | $rwbagcat --key-format=decimal --bin-ips=binary";
my $md5 = "34122422586fe1a3d58f4b5355dbec6b";

check_md5_output($md5, $cmd);
