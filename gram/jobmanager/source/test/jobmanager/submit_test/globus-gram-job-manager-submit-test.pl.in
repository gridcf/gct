#! /usr/bin/perl

use Globus::GRAM::Error;
use Globus::Core::Paths;
use IO::File;
use Test::More;
use File::Compare;
use File::Temp;
use File::Basename qw(dirname);

$ENV{PATH} = "$Globus::Core::Paths::bindir".":"."$Globus::Core::Paths::sbindir".":".$ENV{PATH};
my (@tests, @todo) = ();
my $contact = $ENV{CONTACT_STRING};
my $testtmp = File::Temp::tempdir(CLEANUP => 1);
my $lrm = $ENV{CONTACT_LRM} if exists($ENV{CONTACT_LRM});
my $testdatadir = $ENV{TEST_DATA_DIR};
if (!$testdatadir)
{
    $testdatadir = dirname($0);
}
my $verbose = 0;

my @test_cases=qw(
    submit001 submit002 submit003 submit004 submit005
    submit006 submit007 submit008 submit009 submit010
    submit011 submit012 submit013 submit014 submit015
    submit016 submit017 submit018 submit019 submit020
    submit021 submit022 submit023 submit024 submit025
    submit026 submit027 submit028 submit029 submit030
    submit031 submit032 submit033 submit034 submit035
    submit036 submit037 submit038 submit039 submit040
    submit041 submit042 submit043 submit044 submit045
    submit046 submit047 submit048 submit049
);

my @todo_cases=qw(
);

sub test_rsl
{
    my $testname = shift;
    my $additional_rsl = shift;
    my $testrsl = "$testname.rsl";
    my $additionalrslfile = "$testtmp/$testname.rsl";
    my $testout = "$testtmp/$testname.out";
    my $testerr = "$testtmp/$testname.err";
    my $test_rsl_fp = new IO::File("$testdatadir/$testrsl", '<');
    my $out_rsl_fp = new IO::File($additionalrslfile, '>');
    my $skiplist='';
    my @all_rsl;
    my $globus_location_skip = 0;
    my @lrm_skip_list=();

    while(<$test_rsl_fp>)
    {
	push(@all_rsl, $_);

	$out_rsl_fp->print($_);
    }
    $test_rsl_fp->close();

    if($additional_rsl ne "")
    {
	push(@all_rsl, $additional_rsl);
	$out_rsl_fp->print($additional_rsl);
    }
    $out_rsl_fp->close();

#
    $skiplist = join('', @all_rsl);
    $skiplist =~ m/\S*\(\*\s*(.*)\s*\*\)\S*/;
    $skiplist = $1;

    if ($skiplist)
    {
        foreach my $s (split(/\s+/, $skiplist))
        {
            if ($s =~ m/LRM_SKIP:(.*)/)
            {
                push(@lrm_skip_list, $1);
            }
            if ($s =~ m/GLOBUS_LOCATION_REQUIRED/ && 
                ((!$ENV{GLOBUS_LOCATION}) || (! -d $ENV{GLOBUS_LOCATION})))
            {
                $globus_location_skip = 1;
            }
        }
    }

    SKIP: {
        skip "Skipping test for $lrm", 1,
            if (defined($lrm) && grep(/$lrm/, @lrm_skip_list));
        skip "Skipping test because no GLOBUS_LOCATION", 1
            if ($globus_location_skip != 0);

        if($verbose)
        {
            print "# Submitting job\n";

            foreach(@all_rsl)
            {
                print "#    $_";
            }
            print "\n" if($_ !~ /\n/m);
        }


        {
            local(*OLDOUT, *OLDERR);
            open(OLDOUT, ">&STDOUT");
            open(OLDERR, ">&STDERR");
            open(STDOUT, ">$testout");
            open(STDERR, ">$testerr");

            system("globusrun",
                    "-s", "-r", $contact, "-f", $additionalrslfile);

            $rc = $? >> 8 ||
                  File::Compare::compare("$testdatadir/$testname.out", $testout) ||
                  File::Compare::compare("$testdatadir/$testname.err", $testerr);

            open(STDOUT, ">&OLDOUT");
            open(STDERR, ">&OLDERR");
            close(OLDOUT);
            close(OLDERR);
        }

        ok("$testname:$additional_rsl:$rc" eq "$testname:$additional_rsl:0", "$testname:$additional_rsl");
    }
}

foreach(@test_cases)
{
    push(@tests, "test_rsl(\"$_\", \"\")");
}

push(@tests, "test_rsl(\"" . $test_cases[0] . "\", \"\")");
push(@tests, "test_rsl(\"" . $test_cases[0] . "\", \"(save_state = yes)\")");
push(@tests, "test_rsl(\"" . $test_cases[0] . "\", \"(two_phase = 10)\")");
push(@tests, "test_rsl(\"" . $test_cases[0] . "\", \"(save_state = yes)(two_phase = 10)\")");

foreach(@todo_cases)
{
    push(@tests, "test_cases(\"$_\", \"\")");
    push(@todo, scalar(@tests));
}

if(@ARGV)
{
    my @doit;

    $verbose = 1;


    foreach(@ARGV)
    {
        if(/^(\d+)-(\d+)$/)
        {
            foreach($1 .. $2)
            {
               push(@doit, $_);
            }
        }
        elsif(/^(\d+)$/)
        {
            push(@doit, $1);
        }
    }
    plan tests => scalar(@doit);

    foreach (@doit)
    {
        eval "&$tests[$_-1]";
    }
}
else
{
    plan tests => scalar(@tests), todo => \@todo;

    foreach (@tests)
    {
	eval "&$_";
    }
}
