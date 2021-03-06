#! /usr/bin/perl -w
# MD5: df35e35989e712fa96925b955d6409ac
# TEST: ./rwpmaplookup --country-codes=../../tests/fake-cc.pmap --fields=value --no-title -delim --no-files 10.10.10.10

use strict;
use SiLKTests;

my $rwpmaplookup = check_silk_app('rwpmaplookup');
my %file;
$file{fake_cc} = get_data_or_exit77('fake_cc');
my $cmd = "$rwpmaplookup --country-codes=$file{fake_cc} --fields=value --no-title -delim --no-files 10.10.10.10";
my $md5 = "df35e35989e712fa96925b955d6409ac";

check_md5_output($md5, $cmd);
