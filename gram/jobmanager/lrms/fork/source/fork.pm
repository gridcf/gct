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

# Globus::GRAM::JobManager::fork package
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

use Config;
use IPC::Open2;

package Globus::GRAM::JobManager::fork;

@ISA = qw(Globus::GRAM::JobManager);

my ($mpirun, $mpiexec, $log_path);
my ($starter_in, $starter_out, $starter_index) = (undef, undef, 0);
my %signo;

BEGIN
{
    my $i = 0;

    foreach (split(' ', $Config::Config{sig_name})) 
    {
        $signo{$_} = $i++;
    }

    my $config = new Globus::Core::Config(
        '${sysconfdir}/globus/globus-fork.conf');

    if ($config)
    {
        $mpirun = $config->get_attribute("mpirun") || "no";
        if ($mpirun ne "no" && ! -x $mpirun)
        {
            $mpirun = "no";
        }
        $mpiexec = $config->get_attribute("mpiexec") || "no";
        if ($mpiexec ne "no" && ! -x $mpiexec)
        {
            $mpiexec = "no";
        }
        $softenv_dir = $config->get_attribute("softenv_dir") || "";
        $log_path = $config->get_attribute("log_path") || "/dev/null";
    }
    else
    {
        $mpirun = "no";
        $mpiexec = "no";
        $softenv_dir = "";
        $log_path = "/dev/null";
    }
}

sub new
{
    my $proto = shift;
    my $class = ref($proto) || $proto;
    my $self = $class->SUPER::new(@_);
    my $description = $self->{JobDescription};
    my $stdout = $description->stdout();
    my $stderr = $description->stderr();

    if($description->jobtype() eq 'multiple' && $description->count > 1)
    {
        $self->{STDIO_MERGER} =
            new Globus::GRAM::StdioMerger($self->job_dir(), $stdout, $stderr);
    }
    else
    {
        $self->{STDIO_MERGER} = 0;
    }

    return $self;
}

sub submit
{
    my $self = shift;
    my $cmd;
    my $pid;
    my $pgid;
    my @job_id;
    my $count;
    my $multi_output = 0;
    my $description = $self->{JobDescription};
    my $pipe;
    my @cmdline;
    my @environment;
    my @arguments;
    my $fork_starter = "$Globus::Core::Paths::sbindir/globus-fork-starter";
    my $streamer = "$Globus::Core::Paths::sbindir/globus-gram-streamer";
    my $is_grid_monitor = 0;
    my $soft_msc       = "$softenv_dir/bin/soft-msc";
    my $softenv_load   = "$softenv_dir/etc/softenv-load.sh";

    $starter_index++;

    if(!defined($description->directory()))
    {
        return Globus::GRAM::Error::RSL_DIRECTORY;
    }
    if ($description->directory() =~ m|^[^/]|) {
        $description->add("directory",
                $ENV{HOME} . '/' . $description->directory());
    }
    chdir $description->directory() or
        return Globus::GRAM::Error::BAD_DIRECTORY;

    @environment = $description->environment();
    foreach $tuple ($description->environment())
    {
        if(!ref($tuple) || scalar(@$tuple) != 2)
        {
            return Globus::GRAM::Error::RSL_ENVIRONMENT();
        }
        $CHILD_ENV{$tuple->[0]} = $tuple->[1];
    }

    if(ref($description->count()) ||
       $description->count() != int($description->count()))
    {
        return Globus::GRAM::Error::INVALID_COUNT();
    }
    if($description->jobtype() eq 'multiple')
    {
        $count = $description->count();
        $multi_output = 1 if $count > 1;
    }
    elsif($description->jobtype() eq 'single')
    {
        $count = 1;
    }
    elsif($description->jobtype() eq 'mpi' && $mpiexec ne 'no')
    {
        $count = 1;
        @cmdline = ($mpiexec, '-n', $description->count());
    }
    elsif($description->jobtype() eq 'mpi' && $mpirun ne 'no')
    {
        $count = 1;
        @cmdline = ($mpirun, '-np', $description->count());
    }
    else
    {
        return Globus::GRAM::Error::JOBTYPE_NOT_SUPPORTED();
    }
    if( $description->executable eq "")
    {
        return Globus::GRAM::Error::RSL_EXECUTABLE();
    }
    elsif(! -e $description->executable())
    {
        return Globus::GRAM::Error::EXECUTABLE_NOT_FOUND();
    }
    elsif( (! -x $description->executable())
        || (! -f $description->executable()))
    {
        return Globus::GRAM::Error::EXECUTABLE_PERMISSIONS();
    }
    elsif( $description->stdin() eq "")
    {
        return Globus::GRAM::Error::RSL_STDIN;
    }
    elsif(! -r $description->stdin())
    {
        return Globus::GRAM::Error::STDIN_NOT_FOUND();
    }

    if ($description->executable() =~ m:^(/|\.):) {
        push(@cmdline, $description->executable());
    } else {
        push(@cmdline,
                $description->directory()
                . '/'
                . $description->executable());
    }

    # Check if this is the Condor-G grid monitor
    my $exec = $description->executable();
    my $file_out = `/usr/bin/file $exec`;
    if ( $file_out =~ /script/ || $file_out =~ /text/ ||
	 $file_out =~ m|/usr/bin/env| ) {
	open( EXEC, "<$exec" ) or
	    return Globus::GRAM::Error::EXECUTABLE_PERMISSIONS();
	while( <EXEC> ) {
	    if ( /Sends results from the grid_manager_monitor_agent back to a/ ) {
		$is_grid_monitor = 1;
	    }
	}
	close( EXEC );
    }

    # Reject jobs that want streaming, if so configured, but not for
    # grid monitor jobs
    if ( $description->streamingrequested() &&
	 $description->streamingdisabled() && !$is_grid_monitor ) {

	$self->log("Streaming is not allowed.");
	return Globus::GRAM::Error::OPENING_STDOUT;
    }

    @arguments = $description->arguments();
    foreach(@arguments)
    {
        if(ref($_))
        {
            return Globus::GRAM::Error::RSL_ARGUMENTS;
        }
    }
    if ($#arguments >= 0)
    {
        push(@cmdline, @arguments);
    }

    if ($description->use_fork_starter())
    {
        if (!-x $fork_starter)
        {
            for my $path_component (split(/:/, $ENV{PATH}))
            {
                if (-x "$path_component/globus-fork-starter") {
                    $fork_starter ="$path_component/globus-fork-starter";
                    last;
                }
            }
        }
    }

    if ($description->use_fork_starter() && -x $fork_starter)
    {
        if (!defined($starter_in) && !defined($starter_out))
        {
            $pid = IPC::Open2::open2($starter_out, $starter_in,
                    "$fork_starter $log_path");
            my $oldfh = select $starter_out;
            $|=1;
            select $oldfh;
        }

        print $starter_in "100;perl-fork-start-$$-$starter_index;";
        
        print $starter_in 'directory='.
            &escape_for_starter($description->directory()) . ';';

        if (keys %CHILD_ENV > 0) {
            print $starter_in 'environment='.
                join(',', map { &escape_for_starter($_)
                        .'='.&escape_for_starter($CHILD_ENV{$_})
                    } (keys %CHILD_ENV)) . ';';
        }

        print $starter_in "count=$count;";

        my @softenv = $description->softenv();
        my $enable_default_software_environment
            = $description->enable_default_software_environment();
        if (   ($softenv_dir ne '')
            && (@softenv || $enable_default_software_environment))
        {
            ### SoftEnv extension ###
            $cmd_script_name = $self->job_dir() . '/scheduler_fork_cmd_script';
            local(*CMD);
            open( CMD, '>' . $cmd_script_name );

            print CMD "#!/bin/sh\n";

            $self->setup_softenv(
                $self->job_dir() . '/fork_softenv_cmd_script',
                $soft_msc,
                $softenv_load,
                *CMD);

            print CMD 'cd ', $description->directory(), "\n";
            print CMD "@cmdline\n";
            print CMD "exit \$?\n";

            close(CMD);
            chmod 0700, $cmd_script_name;

            print $starter_in 'executable=' .
                &escape_for_starter($cmd_script_name). ';';
            print $starter_in 'arguments=;';
            #########################
        }
        else
        {
            print $starter_in 'executable=' .
                    &escape_for_starter($cmdline[0]). ';';
            shift @cmdline;
            if ($#cmdline >= 0)
            {
                print $starter_in 'arguments=' .
                        join(',', map {&escape_for_starter($_)} @cmdline) .
                        ';';
            }
        }
        
        my @job_stdout;
        my @job_stderr;

        for ($i = 0; $i < $count; $i++) {
            if($multi_output)
            {
                push(@job_stdout, $self->{STDIO_MERGER}->add_file('out'));
                push(@job_stderr, $self->{STDIO_MERGER}->add_file('err'));
            }
            else
            {
                if (defined($description->stdout)) {
                    push(@job_stdout, $description->stdout());
                } else {
                    push(@job_stdout, '/dev/null');
                }

                if (defined($description->stderr)) {
                    push(@job_stderr, $description->stderr());
                } else {
                    push(@job_stderr, '/dev/null');
                }
            }
        }

        print $starter_in "stdin=" . &escape_for_starter($description->stdin()).
                ';';
        print $starter_in "stdout=" .
                join(',', map {&escape_for_starter($_)} @job_stdout) . ';';
        print $starter_in "stderr=" .
                join(',', map {&escape_for_starter($_)} @job_stderr) . "\n";

        while (<$starter_out>) {
            chomp;
            my @res = split(/;/, $_);

            if ($res[1] ne "perl-fork-start-$$-$starter_index") {
                next;
            }
            if ($res[0] == '101') {
                @job_id = split(',', $res[2]);
                last;
            } elsif ($res[0] == '102') {
                $self->respond({GT3_FAILURE_MESSAGE => "starter: $res[3]" });
                return new Globus::GRAM::Error($res[2]);
            }
        }

        if ($is_grid_monitor) 
        {
            if (! -x $streamer)
            {
                for my $path_component (split(/:/, $ENV{PATH})) {
                    if (-x "$path_component/$globus-gram-streamer")
                    {
                        $streamer = "$path_component/$globus-gram-streamer";
                        last;
                    }
                }
            }
        }
        if ($is_grid_monitor && -x $streamer)
        {
            my $streamer_startup='';

            $starter_index++;
            $streamer_startup .= "100;perl-fork-start-$$-$starter_index;";
            $streamer_startup .= 'directory='.$self->job_dir().';';

            if (keys %CHILD_ENV > 0) {
                $streamer_startup .= 'environment='.
                    join(',', map { &escape_for_starter($_)
                            .'='.&escape_for_starter($CHILD_ENV{$_})
                        } (keys %CHILD_ENV)) . ';';
            }

            $streamer_startup .= "count=1;";

            $streamer_startup .= 'executable=' .  &escape_for_starter($streamer)
                . ';';
            @cmdline = ('-s', $description->state_file());
            foreach my $p (@job_id)
            {
                my $q = $p;
                # strip leading uuid
                $q =~ s/.*://;
                push(@cmdline, '-p', $q);
            }
            $streamer_startup .= 'arguments=' .
                    join(',', map {&escape_for_starter($_)} @cmdline) .
                    ';';
            
            $streamer_startup .= "stdin=/dev/null;";
            $streamer_startup .= "stdout=gram_streamer_out;";
            $streamer_startup .= "stderr=gram_streamer_err\n";

            $self->log("streamer_startup is $streamer_startup");
            print $starter_in $streamer_startup;

            while (<$starter_out>) {
                chomp;
                my @res = split(/;/, $_);

                if ($res[1] ne "perl-fork-start-$$-$starter_index") {
                    next;
                }
                if ($res[0] == '101') {
                    @job_id = (@job_id, split(',', $res[2]));
                    last;
                } elsif ($res[0] == '102') {
                    $self->respond({GT3_FAILURE_MESSAGE => "starter: $res[3]" });
                    return new Globus::GRAM::Error($res[2]);
                }
            }
        }
        $description->add('jobid', join(',', @job_id));
        return { JOB_STATE => Globus::GRAM::JobState::ACTIVE,
                 JOB_ID => join(',', @job_id) };
    } else {
        my $starter_pid;
        local(*READER,*WRITER);        # always use local on perl FDs
        pipe(READER, WRITER);

        $starter_pid = fork();

        if (! defined($starter_pid))
        {
            $failure_code = "fork:$!";
            $self->respond({GT3_FAILURE_MESSAGE => $failure_code });
            return Globus::GRAM::Error::JOB_EXECUTION_FAILED;
        }
        elsif ($starter_pid == 0)
        {
            # Starter Process
            close(READER);

            local(*JOB_READER, *JOB_WRITER);
            pipe(JOB_READER, JOB_WRITER);

            for(my $i = 0; $i < $count; $i++)
            {
                if($multi_output)
                {
                    $job_stdout = $self->{STDIO_MERGER}->add_file('out');
                    $job_stderr = $self->{STDIO_MERGER}->add_file('err');
                }
                else
                {
                    $job_stdout = $description->stdout();
                    $job_stderr = $description->stderr();
                }

                # obtain plain old pipe into temporary variables
                local $^F = 2;                # assure close-on-exec for pipe FDs
                select((select(WRITER),$|=1)[$[]);

                if( ($pid=fork()) == 0)
                {
                    close(JOB_READER);

                    # forked child
                    %ENV = %CHILD_ENV;

                    close(STDIN);
                    close(STDOUT);
                    close(STDERR);

                    open(STDIN, '<' . $description->stdin());
                    open(STDOUT, ">>$job_stdout");
                    open(STDERR, ">>$job_stderr");
                    
                    # the below should never fail since we just forked
                    setpgrp(0,$$);

                    if ( ! exec (@cmdline) )
                    {
                        my $err = "$!\n";
                        $SIG{PIPE} = 'IGNORE';
                        print JOB_WRITER "$err";
                        close(JOB_WRITER);
                        exit(1);
                    }
                }
                else
                {
                    my $error_code = '';

                    if ($pid == undef)
                    {
                        $self->log("fork failed\n");
                        $failure_code = "fork: $!";
                    }
                    close(JOB_WRITER);
                    
                    $_ = <JOB_READER>;
                    close(JOB_READER);

                    if($_ ne '')
                    {
                        chomp($_);
                        $self->log("exec failed\n");
                        $failure_code = "exec: $_";
                    }

                    if ($failure_code ne '')
                    {
                        # fork or exec failed. kill rest of job and return an error
                        $failure_code =~ s/\n/\\n/g;
                        foreach(@job_id)
                        {
                            $pgid = getpgrp($_);

                            $pgid == -1 ? kill($signo{TERM}, $_) :
                                kill(-$signo{TERM}, $pgid);

                            sleep(5);

                            $pgid == -1 ? kill($signo{KILL}, $_) :
                                kill(-$signo{KILL}, $pgid);

                        }

                        local(*ERR);
                        open(ERR, '>' . $description->stderr());
                        print ERR "$failure_code\n";
                        close(ERR);

                        print WRITER "FAIL:$failure_code\n";
                        exit(1);
                    }
                    push(@job_id, $pid);
                }
            }
            if ($is_grid_monitor)
            {
                # Create an extra process to stream output for grid monitor

                # obtain plain old pipe into temporary variables
                local $^F = 2; # assure close-on-exec for pipe FDs
                select((select(WRITER),$|=1)[$[]);

                if( ($pid=fork()) == 0)
                {
                    close(JOB_READER);

                    # forked child
                    %ENV = %CHILD_ENV;

                    close(STDIN);
                    close(STDOUT);
                    close(STDERR);

                    chdir $self->job_dir();

                    open(STDIN, '<' . $description->stdin());
                    open(STDOUT, '>gram_streamer_out');
                    open(STDERR, '>gram_streamer_error');
                    select STDERR; $| = 1;      # make unbuffered
                    select STDOUT; $| = 1;      # make unbuffered

                    
                    # the below should never fail since we just forked
                    setpgrp(0,$$);

                    @cmdline = ($streamer, '-s', $description->state_file());
                    foreach my $p (@job_id)
                    {
                        push(@cmdline, '-p', $p);
                    }

                    if ( ! exec (@cmdline) )
                    {
                        my $err = "$!\n";
                        $SIG{PIPE} = 'IGNORE';
                        print JOB_WRITER "$err";
                        close(JOB_WRITER);
                        exit(1);
                    }
                }
                else
                {
                    my $error_code = '';

                    if ($pid == undef)
                    {
                        $self->log("fork failed\n");
                        $failure_code = "fork: $!";
                    }
                    close(JOB_WRITER);
                    
                    $_ = <JOB_READER>;
                    close(JOB_READER);

                    if($_ ne '')
                    {
                        chomp($_);
                        $self->log("exec failed\n");
                        $failure_code = "exec: $_";
                    }

                    if ($failure_code ne '')
                    {
                        # fork or exec failed. kill rest of job and return an error
                        $failure_code =~ s/\n/\\n/g;
                        foreach(@job_id)
                        {
                            $pgid = getpgrp($_);

                            $pgid == -1 ? kill($signo{TERM}, $_) :
                                kill(-$signo{TERM}, $pgid);

                            sleep(5);

                            $pgid == -1 ? kill($signo{KILL}, $_) :
                                kill(-$signo{KILL}, $pgid);

                        }

                        local(*ERR);
                        open(ERR, '>' . $description->stderr());
                        print ERR "$failure_code\n";
                        close(ERR);

                        print WRITER "FAIL:$failure_code\n";
                        exit(1);
                    }
                    push(@job_id, $pid);
                }
            }
            print WRITER "SUCCESS:" . join(',', @job_id) . "\n";
            exit(0);
        }
        else
        {
            my ($res, $value);
            close(WRITER);
            $_ = <READER>;
            close(READER);
            chomp($_);
            waitpid($starter_pid, 0); 
            ($res, $value) = split(/:/, $_, 2);

            if ($res eq 'SUCCESS')
            {
                $description->add('jobid', $value);
                return { JOB_STATE => Globus::GRAM::JobState::ACTIVE,
                         JOB_ID => $value };
            }
            elsif ($res eq 'FAIL')
            {
                $self->respond({GT3_FAILURE_MESSAGE => "$value" });
                return Globus::GRAM::Error::JOB_EXECUTION_FAILED;
            }
            else
            {
                return Globus::GRAM::Error::JOB_EXECUTION_FAILED;
            }
        }
    }
}

sub poll
{
    my $self = shift;
    my $description = $self->{JobDescription};
    my $state;

    my $jobid = $description->jobid();

    if(!defined $jobid)
    {
        $self->log("poll: job id defined!");
        return { JOB_STATE => Globus::GRAM::JobState::FAILED };
    }

    $self->log("polling job " . $jobid);
    $_ = kill(0, split(/,/, $jobid));

    if($_ > 0)
    {
        $state = Globus::GRAM::JobState::ACTIVE;
    }
    else
    {
        $state = Globus::GRAM::JobState::DONE;
    }
    if($self->{STDIO_MERGER})
    {
        $self->{STDIO_MERGER}->poll($state == Globus::GRAM::JobState::DONE);
    }

    return { JOB_STATE => $state };
}

sub cancel
{
    my $self = shift;
    my $description = $self->{JobDescription};
    my $pgid;
    my $jobid = $description->jobid();

    if(!defined $jobid)
    {
        $self->log("cancel: no jobid defined!");
        return { JOB_STATE => Globus::GRAM::JobState::FAILED };
    }

    $self->log("cancel job " . $jobid);

    foreach (split(/,/,$jobid))
    {
        s/..*://;
        $pgid = getpgrp($_);
        
        $pgid == -1 ? kill($signo{TERM}, $_) :
            kill(-$signo{TERM}, $pgid);

        sleep(5);
        
        $pgid == -1 ? kill($signo{KILL}, $_) :
            kill(-$signo{KILL}, $pgid);
    }

    return { JOB_STATE => Globus::GRAM::JobState::FAILED };
}

sub stage_out
{
    my $self = shift;
    my $description = $self->{JobDescription};
    my $is_grid_monitor = 0;
    my $job_dir = $self->job_dir();
    my $rc;
    my $line;
    my @fields;
    my $stream_out;

    $self->log("Fork stage out");
    # Check if this is the Condor-G grid monitor
    my $exec = $description->executable();
    my $file_out = `/usr/bin/file $exec`;
    if ( $file_out =~ /script/ || $file_out =~ /text/ ||
	 $file_out =~ m|/usr/bin/env| ) {
	open( EXEC, "<$exec" ) or
	    return Globus::GRAM::Error::EXECUTABLE_PERMISSIONS();
	while( <EXEC> ) {
	    if ( /Sends results from the grid_manager_monitor_agent back to a/ ) {
		$is_grid_monitor = 1;
	    }
	}
	close( EXEC );
    }

    if ($is_grid_monitor)
    {
        $self->log("Fork stage out is grid monitor");
        local(*STREAMER_ERROR, *STREAMER_OUTPUT);

        $rc = open(STREAMER_ERROR, "<$job_dir/gram_streamer_error");
        if (!$rc)
        {
            $self->log("Error opening gram_streamer_error: $!");
        }
        chomp($line = <STREAMER_ERROR>);
        if ($line eq '')
        {
            $self->log("No error from streamer");
        }
        else
        {
            $self->log("Error from streamer $line");
            @fields = split(':', $line, 2);

            $self->respond({GT3_FAILURE_MESSAGE => "$fields[0]" });
            return new Globus::GRAM::Error($fields[1]);
        }
        close(STREAMER_ERROR);

        $stream_out = $description->get('file_stream_out');
        $rc = open(STREAMER_OUTPUT, "<$job_dir/gram_streamer_out");
        if (!$rc)
        {
            $self->log("Error opening gram_streamer_output: $!");
        }
        else
        {
            while ($line = <STREAMER_OUTPUT>)
            {
                chomp($line);
                $self->log("Streamer output: $line");
                my ($from, $to) = split(' ', $line);
                
                for (my $i = 0; $i < scalar(@{$stream_out}); $i++)
                {
                    my $pair = $stream_out->[$i];
                    if ($pair->[0] eq $from && $pair->[1] eq $to)
                    {
                        splice(@{$stream_out}, $i, 1);
                        last;
                    }
                }

                $self->respond({'STAGED_STREAM' => "$from $to"});
            }
            $description->add('filestreamout', $stream_out);
        }
    }
    return $self->SUPER::stage_out();
}

sub escape_for_starter
{
    my $str = shift;

    $str =~ s/\\/\\\\/g;
    $str =~ s/;/\\;/g;
    $str =~ s/,/\\,/g;
    $str =~ s/\n/\\n/g;
    $str =~ s/=/\\=/g;

    return $str;
}

END
{
    if (defined($starter_in))
    {
        close($starter_in);
    }
    if (defined($starter_out))
    {
        close($starter_out);
    }
}
1;
