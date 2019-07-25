# Copyright 1999-2006 University of Chicago
# 
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
# 
# http://www.apache.org/licenses/LICENSE-2.0
# 
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

# Globus::GRAM::JobManager::condor package
#
# CVS Information:
# $Source$
# $Date$
# $Revision$
# $Author$

use Globus::GRAM::Error;
use Globus::GRAM::JobState;
use Globus::GRAM::JobManager;
use Globus::GRAM::StdioMerger;
use Globus::Core::Paths;
use Globus::Core::Config;
use IPC::Open3;

# Load OSG group accounting module, if possible
eval { require Globus::GRAM::JobManager::condor_accounting_groups };
our $accounting_groups_callout = ! $@; # Don't make callout if we couldn't load module


package Globus::GRAM::JobManager::condor;

@ISA = qw(Globus::GRAM::JobManager);

my ($condor_submit, $condor_rm, $condor_config, $isNFSLite);

BEGIN
{
    my $config = new Globus::Core::Config(
            '${sysconfdir}/globus/globus-condor.conf');

    $condor_submit = $config->get_attribute("condor_submit") || "no";
    $condor_rm = $config->get_attribute("condor_rm") || "no";
    $condor_arch = $config->get_attribute("condor_arch") || undef;
    $condor_os = $config->get_attribute("condor_os") || undef;
    $condor_config = $config->get_attribute("condor_config") || "";
    $condor_check_vanilla_files = $config->get_attribute(
            "check_vanilla_files") || "no";
    $condor_mpi_script = $config->get_attribute("condor_mpi_script") || "no";

    if ($condor_config ne '')
    {
        $ENV{CONDOR_CONFIG} = $condor_config;
    }

    $isNFSLite = 0;
}

sub new
{
    my $proto = shift;
    my $class = ref($proto) || $proto;
    my $self = $class->SUPER::new(@_);
    my $log_dir;
    my $description = $self->{JobDescription};
    my $stdout = $description->stdout();
    my $stderr = $description->stderr();
    my $globus_condor_conf = "$Globus::Core::Paths::sysconfdir/globus-condor.conf";

    if (-r $globus_condor_conf)
    {   
        local(*FH);
        
        if (open(FH, "<$globus_condor_conf"))
        {   
            while(<FH>) {
                chomp;
                if (m/^isNFSLite=([0-9]*)$/) {
                    $isNFSLite = int($1);
                    last;
                }
            }
            close(FH);
        }
    }

    if (! exists($self->{condor_logfile}))
    {
        if(! exists($ENV{GLOBUS_SPOOL_DIR}))
        {
            $log_dir = $self->job_dir(); 
        }
        else
        {
            $log_dir = $ENV{GLOBUS_SPOOL_DIR};
        }
        $self->{condor_logfile} = "$log_dir/condor." . $description->uniq_id();
    }
    if(! -e $self->{condor_logfile}) 
    {
        if ( open(CONDOR_LOG_FILE, '>>' . $self->{condor_logfile}) ) 
        {
            close(CONDOR_LOG_FILE);
        }
    }

    if($description->jobtype() eq 'multiple' && $description->count > 1)
    {
        $self->{STDIO_MERGER} =
            new Globus::GRAM::StdioMerger($self->job_dir(), $stdout, $stderr);
    }
    else
    {
        $self->{STDIO_MERGER} = 0;
    }



    bless $self, $class;
    return $self;
}

sub submit
{
    my $self = shift;
    my $description = $self->{JobDescription};
    my @environment;
    my $environment_string;
    my $script_filename;
    my @requirements;
    my @tmpr;
    my $rank = '';
    my @arguments;
    my $argument_string;
    my @response_text;
    my @submit_attrs;
    my $submit_attrs_string;
    my $multi_output = 0;
    my $pid;
    my $status;
    my ($condor_submit_out, $condor_submit_err);
    my $rc;
    my $scratch_isset = 0; # Flag if the SCRATCH_DIRECTORY environment variable is set indicating likely GRAM job

    # Reject jobs that want streaming, if so configured
    if ( $description->streamingrequested() &&
	 $description->streamingdisabled() ) {

	$self->log("Streaming is not allowed.");
	return Globus::GRAM::Error::OPENING_STDOUT;
    }

    if($description->jobtype() eq 'single' ||
       $description->jobtype() eq 'multiple')
    {
	$universe = 'vanilla';

        if ($description->jobtype() eq 'multiple'
                && ($description->count() > 1)) {
            $multi_output = 1;
        }
    }
    elsif($description->jobtype() eq 'condor')
    {
	$universe = 'standard'
    }
    elsif($description->jobtype() eq 'mpi' && $condor_mpi_script ne 'no')
    {
        $universe = 'parallel';
    }
    else
    {
	return Globus::GRAM::Error::JOBTYPE_NOT_SUPPORTED();
    }

    # Validate some RSL parameters
    if(!defined($description->directory()))
    {
        return Globus::GRAM::Error::RSL_DIRECTORY;
    }
    elsif( $description->stdin() eq '')
    {
	return Globus::GRAM::Error::RSL_STDIN;
    }
    elsif(ref($description->count()) ||
       $description->count() != int($description->count()))
    {
	return Globus::GRAM::Error::INVALID_COUNT();
    }
    elsif( $description->executable eq '')
    {
	return Globus::GRAM::Error::RSL_EXECUTABLE();
    }

    # In the standard universe, we can validate stdin and directory
    # because they will sent to the execution host  by condor transparently.
    if($universe eq 'standard' || $condor_check_vanilla_files eq 'yes')
    {
	if(! -d $description->directory())
	{
            return Globus::GRAM::Error::BAD_DIRECTORY();
	}
	elsif(! -r $description->stdin())
	{
	    return Globus::GRAM::Error::STDIN_NOT_FOUND();
	}
	elsif(! -f $description->executable())
	{
	    return Globus::GRAM::Error::EXECUTABLE_NOT_FOUND();
	}
	elsif(! -x $description->executable())
	{
	    return Globus::GRAM::Error::EXECUTABLE_PERMISSIONS();
	}
    }

    @environment = $description->environment();

    foreach my $tuple ($description->environment())
    {
        if(!ref($tuple) || scalar(@$tuple) != 2)
        {
            return Globus::GRAM::Error::RSL_ENVIRONMENT();
        }
    }

    # NFS lite start
    if ($isNFSLite && !$isManagedFork) {

        my $osg_grid = '';
        my $use_osg_grid = 1;
    
        map {
            if ($_->[0] eq "OSG_GRID") {
                $osg_grid =  $_->[1]; 
            } elsif ($_->[0] eq "OSG_DONT_USE_OSG_GRID_FOR_GL") {
                $use_osg_grid = 0;
            } elsif ($_->[0] eq "LOGNAME") {
                $logname =  $_->[1];
            } elsif ($_->[0] eq "SCRATCH_DIRECTORY") {
                $scratch_isset = 1;
                $scratch_directory =  $_->[1]; 
                $_->[1] = '$_CONDOR_SCRATCH_DIR';
            } elsif ($_->[0] eq "X509_USER_PROXY") {
                $_->[0] = "CHANGED_X509"; 
            }
        } @environment;
    
        if ($scratch_isset) {
            # Remote_InitialDir apparently suppresses the setting of the SCRATCH_DIRECTORY env variable
            push(@environment,["MY_INITIAL_DIR",'$_CONDOR_SCRATCH_DIR']);
        }
        elsif ( $description->directory() =~ m/.+$logname/xms ) {
            # If the directory ends with the logname it might be a globus-job-run job
            # so take control of the initial_dir
            push(@environment,["MY_INITIAL_DIR",'$_CONDOR_SCRATCH_DIR'] );
        }
        else {
            # assume that remote_initialdir is set and the submitter knows what they are
            # doing.
            push(@environment,["MY_INITIAL_DIR",$description->directory()] );
        }
    }       
    # NFS Lite End

    $environment_string = join(';',
                               map {$_->[0] . "=" . $_->[1]} @environment);

    @arguments = $description->arguments();
    foreach (@arguments)
    {
	if(ref($_))
	{
	    return Globus::GRAM::Error::RSL_ARGUMENTS();
	}
    }
    if ($description->directory() =~ m|^[^/]|)
    {
        my $home = (getpwuid($<))[7];

        $description->add('directory', "$home/".$description->directory());
    }
    if ($description->executable() =~ m|^[^/]|)
    {
        $description->add('executable',
                $description->directory() . '/' . $description->executable());
    }
    if ($universe eq 'parallel')
    {
        unshift(@arguments, $description->executable);
        $description->add('executable', $condor_mpi_script);
    }
    if($#arguments >= 0)
    {
	$argument_string = '"' . join(' ',
				map
				{
				    $_ =~ s/'/''/g;
				    $_ =~ s/"/""/g;
				    $_ = "'$_'";
				}
				@arguments) . '"';
    }
    else
    {
	$argument_string = '';
    }

    @submit_attrs = $description->condorsubmit();
    if(defined($submit_attrs[0]))
    {
	foreach $tuple (@submit_attrs)
	{
	    if(!ref($tuple) || scalar(@$tuple) != 2)
	    {
		return Globus::GRAM::Error::RSL_SCHEDULER_SPECIFIC();
	    }
	}
	$submit_attrs_string = join("\n",
				map {$_->[0] . "=" . $_->[1]} @submit_attrs);
    }
    else
    {
	$submit_attrs_string = '';
    }

    my $group;
    if ($accounting_groups_callout) {
       $group = Globus::GRAM::JobManager::condor_accounting_groups::obtain_condor_group(\@environment, $self);
    }

    # Create script for condor submission
    $script_filename = $self->job_dir() . '/scheduler_condor_submit_script';

    local(*SCRIPT_FILE);

    $rc = open(SCRIPT_FILE, ">$script_filename") ;

    if (!$rc)
    {
        return $self->respond_with_failure_extension(
            "open: $script_filename: $!",
            Globus::GRAM::Error::TEMP_SCRIPT_FILE_FAILED());
    }

    $rc = print SCRIPT_FILE "#\n# description file for condor submission\n#\n";
    if (!$rc)
    {
        return $self->respond_with_failure_extension(
            "print: $script_filename: $!",
            Globus::GRAM::Error::TEMP_SCRIPT_FILE_FAILED());
    }
    $rc = print SCRIPT_FILE "Universe = $universe\n";
    if (!$rc)
    {
        return $self->respond_with_failure_extension(
            "print: $script_filename: $!",
            Globus::GRAM::Error::TEMP_SCRIPT_FILE_FAILED());
    }
    $rc = print SCRIPT_FILE "Notification = Never\n";
    if (!$rc)
    {
        return $self->respond_with_failure_extension(
            "print: $script_filename: $!",
            Globus::GRAM::Error::TEMP_SCRIPT_FILE_FAILED());
    }
    $rc = print SCRIPT_FILE "Executable = " . $description->executable . "\n";
    if (!$rc)
    {
        return $self->respond_with_failure_extension(
            "print: $script_filename: $!",
            Globus::GRAM::Error::TEMP_SCRIPT_FILE_FAILED());
    }
    @tmpr = $description->condor_os;
    if (scalar(@tmpr) > 0)
    {
        my $r = "(" .
            join(" || ",
                map {"OpSys == \"$_\""} @tmpr) .
            ")";
        push(@requirements, $r);
    }
    elsif (defined($condor_os))
    {
        my $r = "(" . join(" || ",
            map { "OpSys == \"$_\"" } split(/\s+/, $condor_os)) . ")";
        push(@requirements, $r);
    }
    @tmpr = $description->condor_arch();
    if (scalar(@tmpr) > 0)
    {
        my $r = "(" .
            join(" || ", map {"Arch == \"$_\""} @tmpr) .
                ")";
        push(@requirements, $r);
    }
    elsif (defined($condor_arch))
    {
        my $r = "(" . join(" || ",
            map { "Arch == \"$_\"" } split(/\s+/, $condor_arch)) . ")";
        push(@requirements, $r);
    }
    if($description->min_memory() ne '')
    {
        push(@requirements, " Memory >= " . $description->min_memory());
    }

    if (scalar(@requirements) > 0)
    {
        $rc = print SCRIPT_FILE "Requirements = ", join(" && ", @requirements) ."\n";
    }
    if (!$rc)
    {
        return $self->respond_with_failure_extension(
            "print: $script_filename: $!",
            Globus::GRAM::Error::TEMP_SCRIPT_FILE_FAILED());
    }

    if ($accounting_groups_callout && $group) {
        $name = getpwuid($>);
        $rc = print SCRIPT_FILE "+AccountingGroup = \"$group.$name\"\n" if $group;
        if (!$rc)
        {
            return $self->respond_with_failure_extension(
                "print: $script_filename: $!",
                Globus::GRAM::Error::TEMP_SCRIPT_FILE_FAILED());
        }
    }

    if($rank ne '')
    {
	$rc = print SCRIPT_FILE "$rank\n";
        if (!$rc)
        {
            return $self->respond_with_failure_extension(
                "print: $script_filename: $!",
                Globus::GRAM::Error::TEMP_SCRIPT_FILE_FAILED());
        }
    }

    if ($ENV{X509_USER_PROXY} ne "") {
        $rc = print SCRIPT_FILE "X509UserProxy = $ENV{X509_USER_PROXY}\n";
        if (!$rc)
        {
            return $self->respond_with_failure_extension(
                "print: $script_filename: $!",
                Globus::GRAM::Error::TEMP_SCRIPT_FILE_FAILED());
        }
    }
    $rc = print SCRIPT_FILE "Environment = $environment_string\n";
    if (!$rc)
    {
        return $self->respond_with_failure_extension(
            "print: $script_filename: $!",
            Globus::GRAM::Error::TEMP_SCRIPT_FILE_FAILED());
    }
    $rc = print SCRIPT_FILE "Arguments = $argument_string\n";
    if (!$rc)
    {
        return $self->respond_with_failure_extension(
            "print: $script_filename: $!",
            Globus::GRAM::Error::TEMP_SCRIPT_FILE_FAILED());
    }
    $rc = print SCRIPT_FILE "InitialDir = " . $description->directory() . "\n";
    if (!$rc)
    {
        return $self->respond_with_failure_extension(
            "print: $script_filename: $!",
            Globus::GRAM::Error::TEMP_SCRIPT_FILE_FAILED());
    }
    $rc = print SCRIPT_FILE "Input = " . $description->stdin() . "\n";
    if (!$rc)
    {
        return $self->respond_with_failure_extension(
            "print: $script_filename: $!",
            Globus::GRAM::Error::TEMP_SCRIPT_FILE_FAILED());
    }
    $rc = print SCRIPT_FILE "Log = " . $self->{condor_logfile} . "\n";
    if (!$rc)
    {
        return $self->respond_with_failure_extension(
            "print: $script_filename: $!",
            Globus::GRAM::Error::TEMP_SCRIPT_FILE_FAILED());
    }
    $rc = print SCRIPT_FILE "log_xml = True\n";
    if (!$rc)
    {
        return $self->respond_with_failure_extension(
            "print: $script_filename: $!",
            Globus::GRAM::Error::TEMP_SCRIPT_FILE_FAILED());
    }
    # NFS Lite mode
    if ($isNFSLite && !$isManagedFork) {
        print SCRIPT_FILE "should_transfer_files = YES\n";
        print SCRIPT_FILE "when_to_transfer_output = ON_EXIT\n";
        print SCRIPT_FILE "transfer_output = true\n";
        # GRAM Files to transfer to the worker node
        # Only do this if we are dealing with a GRAM job that has set up a scratch area
        # otherwise we assume it is a globus-job-run or the users is using remote_initialdir
	if ( $scratch_isset ) {
            my $sdir; my @flist;
            opendir($sdir,$scratch_directory);
            my @sfiles = grep { !/^\./} readdir($sdir);
            close $sdir;

            print SCRIPT_FILE "transfer_input_files = ";
	  SFILE:
	    foreach $f ( @sfiles ) {
		$f =~ s{\/\/}{\/}g;
		$f = $scratch_directory . "/" . $f;
		next SFILE if $f eq $description->executable();
		next SFILE if $f eq $description->stdin();
		next SFILE if $f eq $description->stdout();
                push (@flist,"$f");
            }
            print SCRIPT_FILE join(",",@flist);
            print SCRIPT_FILE "\n";
        }
    }
    # End NFS Lite Mode

    $rc = print SCRIPT_FILE "#Extra attributes specified by client\n";
    if (!$rc)
    {
        return $self->respond_with_failure_extension(
            "print: $script_filename: $!",
            Globus::GRAM::Error::TEMP_SCRIPT_FILE_FAILED());
    }
    $rc = print SCRIPT_FILE "$submit_attrs_string\n";
    if (!$rc)
    {
        return $self->respond_with_failure_extension(
            "print: $script_filename: $!",
            Globus::GRAM::Error::TEMP_SCRIPT_FILE_FAILED());
    }
    $rc = print SCRIPT_FILE "X509UserProxy = $ENV{X509_USER_PROXY}\n";
    if (!$rc)
    {
        return $self->respond_with_failure_extension(
            "print: $script_filename: $!",
            Globus::GRAM::Error::TEMP_SCRIPT_FILE_FAILED());
    }       
    # default max_wall_time can be specified in globus-gram-job-manager.rvf
    if($description->max_wall_time() ne '')
    {
        my $max_wall_time = $description->max_wall_time() ;
        $rc = print SCRIPT_FILE "PeriodicRemove= (JobStatus == 2) && ( (time() - EnteredCurrentStatus) > (" . $max_wall_time . " * 60))\n";
        if (!$rc)
        {
            return $self->respond_with_failure_extension(
                "print: $script_filename: $!",
                Globus::GRAM::Error::TEMP_SCRIPT_FILE_FAILED());
        }
    }

    my $shouldtransferfiles = $description->shouldtransferfiles();
    if (defined($shouldtransferfiles))
    {
        $self->log("Adding \"should_transfer_files = $shouldtransferfiles\"\n");
        $rc = print SCRIPT_FILE "should_transfer_files = $shouldtransferfiles\n";
        if (!$rc)
        {
            return $self->respond_with_failure_extension(
                "print: $script_filename: $!",
                Globus::GRAM::Error::TEMP_SCRIPT_FILE_FAILED());
        }
    }
    my @xcount = $description->xcount();
    if(@xcount)
    {   
        $self->log("xcount = " . scalar(@xcount));
        foreach my $this_xcount (@xcount)
        {   
            $self->log("xcount = " . $this_xcount);
            $rc = print SCRIPT_FILE "request_cpus=$this_xcount \n";
            if (!$rc)
                {
                    return $self->respond_with_failure_extension(
                    "print: $script_filename: $!",
                    Globus::GRAM::Error::TEMP_SCRIPT_FILE_FAILED());
                }
        }   
    }

    if($description->min_memory() ne '')
    {
        my $memory_request = $description->min_memory();
        $rc = print SCRIPT_FILE "request_memory=" . $memory_request . "\n";
        if (!$rc)
        {
            return $self->respond_with_failure_extension(
                "print: $script_filename: $!",
                Globus::GRAM::Error::TEMP_SCRIPT_FILE_FAILED());
        }
    }

    my $WhenToTransferOutput = $description->whentotransferoutput();
    if (defined($WhenToTransferOutput))
    {
        $self->log("Adding \"WhenToTransferOutput = $WhenToTransferOutput\"\n");
        $rc = print SCRIPT_FILE "WhenToTransferOutput = $WhenToTransferOutput\n";
        if (!$rc)
        {
            return $self->respond_with_failure_extension(
                "print: $script_filename: $!",
                Globus::GRAM::Error::TEMP_SCRIPT_FILE_FAILED());
        }
    }

    my $transfer_input_files = $description->transferinputfiles();
    if (defined($transfer_input_files))
    {
        $self->log("Adding explicitly \"transfer_input_files = "
                  ."$transfer_input_files\"\n");
        $rc = print SCRIPT_FILE "transfer_input_files = $transfer_input_files\n";
        if (!$rc)
        {
            return $self->respond_with_failure_extension(
                "print: $script_filename: $!",
                Globus::GRAM::Error::TEMP_SCRIPT_FILE_FAILED());
        }
    }
    else
    {
        my @transfer_input_files = $description->transferinputfiles();
        if (defined($transfer_input_files[0]))
            {
            my $file_list_string = "";
            foreach my $file (@transfer_input_files)
            {
                $file_list_string .= "$file, ";
            }
            $file_list_string =~ s/, $//;
            $self->log("Adding \"transfer_input_files = $file_list_string\"\n");
            $rc = print SCRIPT_FILE "transfer_input_files = $file_list_string\n";
            if (!$rc)
            {
                return $self->respond_with_failure_extension(
                    "print: $script_filename: $!",
                    Globus::GRAM::Error::TEMP_SCRIPT_FILE_FAILED());
            }
        }
    }

    my $transfer_output_files = $description->transferoutputfiles();
    if (defined($transfer_output_files))
    {
        $self->log("Adding explicitly \"transfer_output_files = "
                  ."$transfer_output_files\"\n");
        $rc = print SCRIPT_FILE "transfer_output_files = $transfer_output_files\n";
        if (!$rc)
        {
            return $self->respond_with_failure_extension(
                "print: $script_filename: $!",
                Globus::GRAM::Error::TEMP_SCRIPT_FILE_FAILED());
        }
    }
    else
    {
        my @transfer_output_files = $description->transferoutputfiles();
        if (defined($transfer_output_files[0]))
        {
            my $file_list_string = "";
            foreach my $file (@transfer_output_files)
            {
                $file_list_string .= "$file, ";
            }
            $file_list_string =~ s/, $//;
            $self->log("Adding \"transfer_output_files = "
                      ."$file_list_string\"\n");
            $rc = print SCRIPT_FILE "transfer_output_files = $file_list_string\n";
            if (!$rc)
            {
                return $self->respond_with_failure_extension(
                    "print: $script_filename: $!",
                    Globus::GRAM::Error::TEMP_SCRIPT_FILE_FAILED());
            }
        }
    }

    if ($universe eq 'parallel')
    {
        $rc = print SCRIPT_FILE "Output = " . $description->stdout() . "\n" .
                                "Error = " . $description->stderr() . "\n" .
                                "machine_count = " . $description->count() . "\n" .
                                "queue\n";
        if (!$rc)
        {
            return $self->respond_with_failure_extension(
                "print: $script_filename: $!",
                Globus::GRAM::Error::TEMP_SCRIPT_FILE_FAILED());
        }
    }
    else
    {
        for (my $i = 0; $i < $description->count(); $i++) {
            if ($multi_output)
            {
                $rc = print SCRIPT_FILE
                        "Output = " . $self->{STDIO_MERGER}->add_file('out') .
                        "\n" .
                        "Error = " .  $self->{STDIO_MERGER}->add_file('err') .
                        "\n";
                if (!$rc)
                {
                    return $self->respond_with_failure_extension(
                        "print: $script_filename: $!",
                        Globus::GRAM::Error::TEMP_SCRIPT_FILE_FAILED());
                }
            }
            else
            {
                $rc = print SCRIPT_FILE
                        "Output = " . $description->stdout() .  "\n" .
                        "Error = " . $description->stderr() . "\n";
                if (!$rc)
                {
                    return $self->respond_with_failure_extension(
                        "print: $script_filename: $!",
                        Globus::GRAM::Error::TEMP_SCRIPT_FILE_FAILED());
                }
            }
            $rc = print SCRIPT_FILE "queue 1\n";
            if (!$rc)
            {
                return $self->respond_with_failure_extension(
                    "print: $script_filename: $!",
                    Globus::GRAM::Error::TEMP_SCRIPT_FILE_FAILED());
            }
        }
    }

    $rc = close(SCRIPT_FILE);
    if (!$rc)
    {
        return $self->respond_with_failure_extension(
            "close: $script_filename: $!",
            Globus::GRAM::Error::TEMP_SCRIPT_FILE_FAILED());
    }

    $self->log("About to submit condor job");
    local(*SUBMIT_IN);
    local(*SUBMIT_OUT);
    local(*SUBMIT_ERR);
    $pid = IPC::Open3::open3(
            \*SUBMIT_IN, \*SUBMIT_OUT, \*SUBMIT_ERR,
            $condor_submit, $script_filename);
    if (!$pid)
    {
        return $self->respond_with_failure_extension(
            "open3: $condor_submit: $!",
            Globus::GRAM::Error::TEMP_SCRIPT_FILE_FAILED());
    }

    close(SUBMIT_IN);

    local $/;

    $condor_submit_out = <SUBMIT_OUT>;
    $condor_submit_err = <SUBMIT_ERR>;

    close(SUBMIT_OUT);
    close(SUBMIT_ERR);

    waitpid($pid, 0);
    $status = $?>>8;

    $self->log("condor_submit status: $status");
    $self->log("condor_submit output: $condor_submit_out");
    $self->log("condor_submit error: $condor_submit_err");

    if ($status == 0)
    {
        $response_line = (grep(/submitted to cluster/,
                split(/\n/, $condor_submit_out)))[0];

        $job_id = (split(/\./, (split(/\s+/, $response_line))[5]))[0];

	if($job_id ne '')
	{
	    $status = Globus::GRAM::JobState::PENDING;

            $job_id = join(',', map { sprintf("%03d.%03d.%03d",
                    $job_id, $_, 0) } (0..($description->count()-1)));
	    return {JOB_STATE => Globus::GRAM::JobState::PENDING,
		    JOB_ID    => $job_id};
	}
    }
    elsif ($condor_submit_err ne '')
    {
        $self->log("Writing extended error information to stderr");
        local(*ERR);
        open(ERR, '>' . $description->stderr());
        print ERR $condor_submit_err;
        close(ERR);

        $condor_submit_err =~ s/\n/\\n/g;

        return $self->respond_with_failure_extension(
                "condor_submit: $condor_submit_err",
                Globus::GRAM::Error::JOB_EXECUTION_FAILED());
    }
    return Globus::GRAM::Error::JOB_EXECUTION_FAILED;
}

sub poll
{
    my $self = shift;
    my $description = $self->{JobDescription};
    my $state;
    my $job_id = $description->job_id();
    my @job_ids = split(/,/, $description->job_id());
    my ($cluster, $rest) = split(/\./, $job_ids[0], 2);
    my $num_done;
    my $num_run;
    my $num_evict;
    my $num_abort;
    my $record = {};
    local(*CONDOR_LOG_FILE);

    $self->log("polling job " . $description->jobid());

    if ( open(CONDOR_LOG_FILE, '<' . $self->{condor_logfile}) )
    {
        while (<CONDOR_LOG_FILE>)
        {
            if (/<c>/) {
                if (defined($record)) {
                    if ($record->{Cluster} == $cluster)
                    {
                        # record Matches our job id
                        if ($record->{EventTypeNumber} == 1)
                        {
                            # execute event
                            $num_run++;
                        } elsif ($record->{EventTypeNumber} == 4) {
                            $num_evict++;
                        } elsif ($record->{EventTypeNumber} == 5) {
                            $num_done++;
                        } elsif ($record->{EventTypeNumber} == 9) {
                            $num_abort++;
                        }
                    }
                }
                $record = {};
            } elsif (/<a n="([^"]+)">/) { #"/) {
                my $attr = $1;

                if (/<s>([^<]+)<\/s>/) {
                    $record->{$attr} = $1;
                } elsif (/<i>([^<]+)<\/i>/) {
                    $record->{$attr} = int($1);
                } elsif (/<b v="([tf])"\/>/) {
                    $record->{$attr} = ($1 eq 't');
                } elsif (/<r>([^<]+)<\/r>/) {
                    $record->{$attr} = $1;
                }
            } elsif (/<\/c>/) {
            }
        }

        if (defined($record)) {
            if ($record->{Cluster} == $cluster)
            {
                # record Matches our job id
                if ($record->{EventTypeNumber} == 1)
                {
                    # execute event
                    $num_run++;
                } elsif ($record->{EventTypeNumber} == 4) {
                    $num_evict++;
                } elsif ($record->{EventTypeNumber} == 5) {
                    $num_done++;
                } elsif ($record->{EventTypeNumber} == 9) {
                    $num_abort++;
                }
            }
        } 
        @status = grep(/^[0-9]* \(0*${job_id}/, <CONDOR_LOG_FILE>);
        close(CONDOR_LOG_FILE);
    }
    else
    {
        $self->nfssync( $description->stdout(), 0 )
            if $description->stdout() ne '';
        $self->nfssync( $description->stderr(), 0 )
            if $description->stderr() ne '';
        return { JOB_STATE => Globus::GRAM::JobState::DONE };
    }

    if($num_abort > 0)
    {
        $state = Globus::GRAM::JobState::FAILED;
    }
    elsif($num_done == $description->count())
    {
        $self->nfssync( $description->stdout(), 0 )
            if $description->stdout() ne '';
        $self->nfssync( $description->stderr(), 0 )
            if $description->stderr() ne '';

        $state = Globus::GRAM::JobState::DONE;
    }
    elsif($num_run == 0)
    {
        $state = Globus::GRAM::JobState::PENDING;
    }
    elsif($num_run > $num_evict)
    {
        $state = Globus::GRAM::JobState::ACTIVE;
    }
    else
    {
        $state = Globus::GRAM::JobState::SUSPENDED;
    }

    if($self->{STDIO_MERGER}) {
        $self->{STDIO_MERGER}->poll($state == Globus::GRAM::JobState::DONE);
    }

    return { JOB_STATE => $state };
}

sub cancel
{
    my $self = shift;
    my $description = $self->{JobDescription};
    my $job_id = $description->jobid();
    my $count = 0;

    $job_id =~ s/,/ /g;
    $job_id =~ s/(\d+\.\d+)\.\d+/$1/g;

    $self->log("cancel job " . $description->jobid());
    # we do not need to be too efficient here
    $self->log(`$condor_rm $job_id 2>&1`);

    if($? == 0)
    {
	return { JOB_STATE => Globus::GRAM::JobState::FAILED };
    }
    else
    {
	return Globus::GRAM::Error::JOB_CANCEL_FAILED();
    }
}

sub respond_with_failure_extension
{
    my $self = shift;
    my $msg = shift;
    my $rc = shift;

    $self->respond({GT3_FAILURE_MESSAGE => $msg });
    return $rc;
}

1;
