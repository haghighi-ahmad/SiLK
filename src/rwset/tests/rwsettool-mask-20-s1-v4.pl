#! /usr/bin/perl -w
# MD5: 541a5760f79b233d325b7220e8319551
# TEST: ./rwsettool --mask=20 ../../tests/set1-v4.set | ./rwsetcat

use strict;
use SiLKTests;

my $rwsettool = check_silk_app('rwsettool');
my $rwsetcat = check_silk_app('rwsetcat');
my %file;
$file{v4set1} = get_data_or_exit77('v4set1');
my $cmd = "$rwsettool --mask=20 $file{v4set1} | $rwsetcat";
my $md5 = "541a5760f79b233d325b7220e8319551";

check_md5_output($md5, $cmd);
