#! /usr/bin/perl -w
# MD5: 95d57c6002beddf3ccdbdb3122c106f7
# TEST: ./rwsettool --mask=70 ../../tests/set1-v6.set | ./rwsetcat

use strict;
use SiLKTests;

my $rwsettool = check_silk_app('rwsettool');
my $rwsetcat = check_silk_app('rwsetcat');
my %file;
$file{v6set1} = get_data_or_exit77('v6set1');
check_features(qw(ipset_v6));
my $cmd = "$rwsettool --mask=70 $file{v6set1} | $rwsetcat";
my $md5 = "95d57c6002beddf3ccdbdb3122c106f7";

check_md5_output($md5, $cmd);
