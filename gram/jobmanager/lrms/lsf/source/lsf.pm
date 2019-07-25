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

use Globus::GRAM::Error;
use Globus::GRAM::JobState;
use Globus::GRAM::JobManager;
use Globus::Core::Paths;
use Globus::Core::Config;

use IPC::Open3;

use Config;

our %ENV;
# NOTE: This package name must match the name of the .pm file!!
package Globus::GRAM::JobManager::lsf;

@ISA = qw(Globus::GRAM::JobManager);

my ($lsf_profile, $mpirun, $mpiexec, $bsub, $bjobs, $bkill, $bhist);

BEGIN
{
    my $config = new Globus::Core::Config(
	'${sysconfdir}/globus/globus-lsf.conf');
    my @cmds = qw(mpirun mpiexec bsub bjobs bkill bhist);

    if (!defined($config))
    {
        if (exists $ENV{LSF_ENVDIR})
        {
            $lsf_profile = $ENV{LSF_ENVDIR} . "/profile.lsf";
        }
        else
        {
            $lsf_profile = "/etc/profile.lsf"
        }
    }
    else
    {
        $lsf_profile = $config->get_attribute('lsf_profile');
    }

    $ENV{LSF_PROFILE} = $lsf_profile;
    if (-f $lsf_profile)
    {
        my $fh;
        open($fh, ". $lsf_profile >/dev/null 2> /dev/null && env|");
        while (<$fh>)
        {
            chomp;
            my ($var, $val) = split(/=/, $_, 2);
            $ENV{$var} = $val;
	    eval "print \"GRAM_SCRIPT_LOG:msg=\\\"\$var = \$val in env\\\"\\n\"";
        }
        close($fh);
    }

    foreach my $attr (@cmds) {
       my $val;
       if (defined($config))
       {
           eval "${$attr} = $config->get_attribute('$attr')";
           eval "\$val = ${$attr}";
           if ($val eq '') {
              $val = 'no';
              eval "${$attr} = 'no'";
           }
       }
       else
       {
           eval "${$attr} = 'no'";
           eval "\$val = ${$attr}";
       }
       if ($val eq 'no') {
           foreach my $p (split(":", $ENV{PATH})) {
               if (-x "$p/$attr") {
                   $val = "$p/$attr";
                   eval "\${$attr} = \$val;";
                   last;
               }
           }
       }
    }
}

sub submit
{
    my $self = shift;
    my $description = $self->{JobDescription};
    my $status;
    my $lsf_job_err_name;
    my $queue;
    my $job_id;
    my @arguments;
    my $args = '';
    my $old;
    my $pid;
    my $rc;
    my $job_out;
    my $job_err;
    my $status;

    $self->log('Entering lsf submit');

    # Reject jobs that want streaming, if so configured
    if ( $description->streamingrequested() &&
	 $description->streamingdisabled() ) {

	$self->log("Streaming is not allowed.");
	return Globus::GRAM::Error::OPENING_STDOUT;
    }

    # check jobtype
    if(defined($description->jobtype()))
    {
	if($description->jobtype !~ /^(mpi|single|multiple)$/)
	{
	    return Globus::GRAM::Error::JOBTYPE_NOT_SUPPORTED;
	}
	elsif($description->jobtype() eq 'mpi' && $mpirun eq 'no' &&
                $mpiexec eq 'no')
	{
	    return Globus::GRAM::Error::JOBTYPE_NOT_SUPPORTED;
	}
    }

    if( $description->directory eq '')
    {
	return Globus::GRAM::Error::RSL_DIRECTORY;
    }
    if ($description->directory =~ m|^[^/]|) {
        $description->add('directory',
                (getpwuid($<))[7] . '/' . $description->directory);
    }
    if((! -d $description->directory) || (! -r $description->directory))
    {
	return Globus::GRAM::Error::BAD_DIRECTORY;
    }

    # make sure the files are accessible (NFS sync) when you check for them
    $self->nfssync( $description->executable() )
	unless $description->executable() eq '';
    $self->nfssync( $description->stdin() )
	unless $description->stdin() eq '';

    if ($description->executable =~ m|^[^/]|) {
        $description->add('executable',
                $description->directory . '/' . $description->executable);
    }
    if( $description->executable eq '')
    {
	return Globus::GRAM::Error::RSL_EXECUTABLE();
    }
    elsif(! -f $description->executable())
    {
	return Globus::GRAM::Error::EXECUTABLE_NOT_FOUND();
    }
    elsif(! -x $description->executable())
    {
	return Globus::GRAM::Error::EXECUTABLE_PERMISSIONS();
    }
    elsif( $description->stdin() eq '')
    {
	return Globus::GRAM::Error::RSL_STDIN;
    }
    elsif(! -r $description->stdin())
    {
       return Globus::GRAM::Error::STDIN_NOT_FOUND();
   }

    $self->log('Determining job max time cpu from job description');
    if(defined($description->max_cpu_time())) 
    {
	$cpu_time = $description->max_cpu_time();
	$self->log("   using maxcputime of $cpu_time");
    }
    elsif(defined($description->max_time()))
    {
	$cpu_time = $description->max_time();
	$self->log("   using maxtime of $cpu_time");
    }
    else
    {
	$cpu_time = 0;
	$self->log('   using queue default');
    }

    $self->log('Determining job max wall time limit from job description');
    if(defined($description->max_wall_time()))
    {
	$wall_time = $description->max_wall_time();
	$self->log("    using maxwalltime of $wall_time");
    }
    else
    {
	$wall_time = 0;
	$self->log('    using queue default');
    }

    if($description->queue() ne '')
    {
	$queue = $description->queue();
    }

    $self->log('Building job script');

    local(*JOB);
    local(*JOB_OUT);
    local(*JOB_ERR);
    $self->log("using $bsub as bsub");
    $pid = IPC::Open3::open3(JOB, JOB_OUT, JOB_ERR, $bsub);
    if (!$pid)
    {
        return $self->respond_with_failure_extension(
            "open3: $bsub: $!",
            Globus::GRAM::Error::TEMP_SCRIPT_FILE_FAILED());
    }
    $old = select(JOB_OUT); $|=1;
    select(JOB_ERR); $|=1;
    select(JOB); $|=1;
    select($old);

    $rc = print JOB <<"EOF";
#! /bin/sh
#
# LSF batch job script built by Globus Job Manager
#
EOF
    if (!$rc)
    {
        return $self->respond_with_failure_extension(
            "print: $lsf_job_script_name: $!",
            Globus::GRAM::Error::TEMP_SCRIPT_FILE_FAILED());
    }

    if(defined($queue))
    {
	$rc = print JOB "#BSUB -q $queue\n";
        if (!$rc)
        {
            return $self->respond_with_failure_extension(
                "print: $lsf_job_script_name: $!",
                Globus::GRAM::Error::TEMP_SCRIPT_FILE_FAILED());
        }
    }
    if(defined($description->project()))
    {
	$rc = print JOB '#BSUB -P ', $description->project(), "\n";
        if (!$rc)
        {
            return $self->respond_with_failure_extension(
                "print: $lsf_job_script_name: $!",
                Globus::GRAM::Error::TEMP_SCRIPT_FILE_FAILED());
        }
    }

    if($cpu_time != 0)
    {
	if($description->jobtype() eq 'multiple')
	{
	    $total_cpu_time = $cpu_time * $description->count();
	}
	else
	{
	    $total_cpu_time = $cpu_time;
	}
	$rc = print JOB "#BSUB -c ${total_cpu_time}\n";
        if (!$rc)
        {
            return $self->respond_with_failure_extension(
                "print: $lsf_job_script_name: $!",
                Globus::GRAM::Error::TEMP_SCRIPT_FILE_FAILED());
        }
    }

    if (defined($description->name()))
    {
        my $name = $description->name();
        $rc = print JOB "#BSUB -J $name\n";
        if (!$rc)
        {
            return $self->respond_with_failure_extension(
                "print: $lsf_job_script_name: $!",
                Globus::GRAM::Error::TEMP_SCRIPT_FILE_FAILED());
        }
    }

    if($wall_time != 0)
    {
	$rc = print JOB "#BSUB -W $wall_time\n";
        if (!$rc)
        {
            return $self->respond_with_failure_extension(
                "print: $lsf_job_script_name: $!",
                Globus::GRAM::Error::TEMP_SCRIPT_FILE_FAILED());
        }
    }

    if($description->max_memory() != 0)
    {
	$max_memory = $description->max_memory() * 1024;

	if($description->jobtype() eq 'multiple')
	{
	    $total_max_memory = $max_memory * $description->count();
	}
	else
	{
	    $total_max_memory = $max_memory;
	}
	$rc = print JOB "#BSUB -M ${total_max_memory}\n";
        if (!$rc)
        {
            return $self->respond_with_failure_extension(
                "print: $lsf_job_script_name: $!",
                Globus::GRAM::Error::TEMP_SCRIPT_FILE_FAILED());
        }
    }
    $rc = print JOB '#BSUB -i ', $description->stdin(), "\n";
    if (!$rc)
    {
        return $self->respond_with_failure_extension(
            "print: $lsf_job_script_name: $!",
            Globus::GRAM::Error::TEMP_SCRIPT_FILE_FAILED());
    }
    $rc = print JOB '#BSUB -o ', $description->stdout(), "\n";
    if (!$rc)
    {
        return $self->respond_with_failure_extension(
            "print: $lsf_job_script_name: $!",
            Globus::GRAM::Error::TEMP_SCRIPT_FILE_FAILED());
    }
    $self->nfssync( $description->stdout(), 1 );
    $rc = print JOB '#BSUB -e ', $description->stderr(), "\n";
    if (!$rc)
    {
        return $self->respond_with_failure_extension(
            "print: $lsf_job_script_name: $!",
            Globus::GRAM::Error::TEMP_SCRIPT_FILE_FAILED());
    }
    $self->nfssync( $description->stderr(), 1 );
    $rc = print JOB '#BSUB -n ', $description->count(), "\n";
    if (!$rc)
    {
        return $self->respond_with_failure_extension(
            "print: $lsf_job_script_name: $!",
            Globus::GRAM::Error::TEMP_SCRIPT_FILE_FAILED());
    }

    my @xcount, @host_xcount;
    @xcount = $description->xcount();
    @host_xcount = $description->host_xcount();

    if (@host_xcount)
    {
        my $res = "";
        $self->log("host_xcount = " . scalar(@host_xcount));
        foreach my $this_hostxcount (@host_xcount)
        {
            $self->log("host_xcount = " . $this_hostxcount);
            my $this_xcount = shift @xcount;
            if (!$this_xcount)
            {
                $this_xcount = 1;
            }
            my $mult = int($this_xcount) * int($this_hostxcount);

            if ($res ne '')
            {
                $res .= " + ";
            }
            $res .= "$mult * {span[ptile=$this_xcount]}";
        }
        $rc = print JOB "#BSUB -R \"$res\"\n";
        if (!$rc)
        {
            return $self->respond_with_failure_extension(
                "print: $lsf_job_script_name: $!",
                Globus::GRAM::Error::TEMP_SCRIPT_FILE_FAILED());
        }
    }
    elsif (@xcount)
    {
        my $res = "";
        $self->log("xcount = " . scalar(@xcount));
        foreach my $this_xcount (@xcount)
        {
            if ($res ne '')
            {
                $res .= " + ";
            }
            $res .= "$this_xcount * {span[ptile=$this_xcount]}";
        }
        $rc = print JOB "#BSUB -R \"$res\"\n";
        if (!$rc)
        {
            return $self->respond_with_failure_extension(
                "print: $lsf_job_script_name: $!",
                Globus::GRAM::Error::TEMP_SCRIPT_FILE_FAILED());
        }
    }

    foreach my $tuple ($description->environment())
    {
	if(!ref($tuple) || scalar(@$tuple) != 2)
	{
	    return Globus::GRAM::Error::RSL_ENVIRONMENT();
	}
	$rc = print JOB $tuple->[0], '="', $tuple->[1],
		'"; export ', $tuple->[0], "\n";
        if (!$rc)
        {
            return $self->respond_with_failure_extension(
                "print: $lsf_job_script_name: $!",
                Globus::GRAM::Error::TEMP_SCRIPT_FILE_FAILED());
        }
    }

    $rc = print JOB "\n#Change to directory requested by user\n";
    if (!$rc)
    {
        return $self->respond_with_failure_extension(
            "print: $lsf_job_script_name: $!",
            Globus::GRAM::Error::TEMP_SCRIPT_FILE_FAILED());
    }
    $rc = print JOB 'cd ', $description->directory(), "\n";
    if (!$rc)
    {
        return $self->respond_with_failure_extension(
            "print: $lsf_job_script_name: $!",
            Globus::GRAM::Error::TEMP_SCRIPT_FILE_FAILED());
    }

    @arguments = $description->arguments();

    foreach(@arguments)
    {
        if(ref($_))
	{
	    return Globus::GRAM::Error::RSL_ARGUMENTS;
	}
    }
    if($#arguments >= 0)
    {
        foreach(@arguments)
        {
             $_ =~ s/\\/\\\\/g;
	     $_ =~ s/\$/\\\$/g;
	     $_ =~ s/"/\\\"/g; #"
	     $_ =~ s/`/\\\`/g; #`
	     
	     $args .= '"' . $_ . '" ';
        }
    }
    else
    {
	$args = '';
    }

    if ($description->executable() =~ m|^[^/]|)
    {
        $description->add('executable', './' . $description->executable());
    }

    if($description->jobtype() eq 'mpi' && $mpiexec ne 'no')
    {
	$rc = print JOB "$mpiexec -n ", $description->count(), ' ',
                  $description->executable(), " $args \n";
        if (!$rc)
        {
            return $self->respond_with_failure_extension(
                "print: $lsf_job_script_name: $!",
                Globus::GRAM::Error::TEMP_SCRIPT_FILE_FAILED());
        }
    }
    elsif($description->jobtype() eq 'mpi' && $mpirun ne 'no')
    {
	$rc = print JOB "$mpirun -np ", $description->count(), ' ',
	          $description->executable(), " $args \n";
        if (!$rc)
        {
            return $self->respond_with_failure_extension(
                "print: $lsf_job_script_name: $!",
                Globus::GRAM::Error::TEMP_SCRIPT_FILE_FAILED());
        }
    }
    elsif($description->jobtype() eq 'multiple')
    {
        $rc = print JOB "pids=''\n", "exit_code=0\n";
        if (!$rc)
        {
            return $self->respond_with_failure_extension(
                "print: $lsf_job_script_name: $!",
                Globus::GRAM::Error::TEMP_SCRIPT_FILE_FAILED());
        }
	for(my $i = 0; $i < $description->count(); $i++)
	{
	    $rc = print JOB $description->executable(), " $args &\n",
                            "pids=\"\$pids \$!\"\n";
            if (!$rc)
            {
                return $self->respond_with_failure_extension(
                    "print: $lsf_job_script_name: $!",
                    Globus::GRAM::Error::TEMP_SCRIPT_FILE_FAILED());
            }
	}

        $rc = print JOB <<EOF;
        for x in \$pids; do
            wait \$x
            tmp_exit_code=\$?
            if [ \$exit_code = 0 -a \$tmp_exit_code != 0 ]; then
                exit_code=\$tmp_exit_code
            fi
        done
        exit \$exit_code
EOF
        if (!$rc)
        {
            return $self->respond_with_failure_extension(
                "print: $lsf_job_script_name: $!",
                Globus::GRAM::Error::TEMP_SCRIPT_FILE_FAILED());
        }
    }
    else
    {
	$rc = print JOB $description->executable(), " $args\n";
        if (!$rc)
        {
            return $self->respond_with_failure_extension(
                "print: $lsf_job_script_name: $!",
                Globus::GRAM::Error::TEMP_SCRIPT_FILE_FAILED());
        }
    }

    $rc = close(JOB);
    if (!$rc)
    {
        return $self->respond_with_failure_extension(
            "print: $lsf_job_script_name: $!",
            Globus::GRAM::Error::TEMP_SCRIPT_FILE_FAILED());
    }
    chmod 0755, $lsf_job_script_name;

    local $/;
    $job_out = <JOB_OUT>;
    $job_err = <JOB_ERR>;
    waitpid $pid, 0;
    $status = $? >> 8;
    close(JOB_OUT);
    close(JOB_ERR);

    $self->log("bsub output: $job_out") if ($job_out ne '');
    $self->log("bsub error: $job_err") if ($job_err ne '');
    $self->log("bsub exit status: $status");

    if($status == 0)
    {
        $job_id = (grep(/is submitted/, split(/\n/, $job_out)))[0];
	$job_id =~ m/<([^>]*)>/;
	$job_id = $1;

        if ($job_id eq '')
        {
            return Globus::GRAM::Error::JOB_EXECUTION_FAILED;
        }
        else
        {
            return {
	           JOB_ID => $job_id,
		   JOB_STATE => Globus::GRAM::JobState::PENDING
		};
        }
    }
    else
    {
        local(*ERR);
        open(ERR, '>' . $description->stderr());
        print ERR $job_err;
        close(ERR);

        $job_err =~ s/\n/\\n/g;

        $self->respond({ GT3_FAILURE_MESSAGE => $job_err });
    }

    return Globus::GRAM::Error::JOB_EXECUTION_FAILED;
}

sub poll
{
    # The LSF bjobs command is used to obtain the current
    # status of the job. This status is then returned.
    #
    # The Status field can contain one of the following strings:
    #
    # string        stands for                      Globus context meaning
    # --------------------------------------------------------------------
    # RUN           Running                         ACTIVE
    # PEND          Wating to be scheduled          PENDING
    # USUSP         Suspended while running         SUSPENDED
    # PSUSP         Suspended while pending         SUSPENDED
    # SSUSP         Suspended by system             SUSPENDED
    # DONE          Completed sucessfully           DONE
    # EXIT          Completed unsuccessfully        DONE (But command return non-zero exit code)
    # UNKWN         Unknown state                   *ignore*
    # ZOMBI         Unknown state                   FAILED

    my $self = shift;
    my $description = $self->{JobDescription};
    my $job_id = $description->jobid();
    my $state;
    my $status_line;
    my $exit_code;

    $self->log("polling job $job_id");

    # Get first line matching job id
    # needs to be back-ticks to source lsf profile
    $_ = (grep(/$job_id/, `$bjobs -a $job_id 2>/dev/null`))[0];

    # get the exit code of the bjobs command.  For more info, do a 
    # search for $CHILD_ERROR in perlvar documentation.
    $exit_code = $? >> 8;

    # Verifying that the job is no longer there.
    # return code 255 = "Job <123> is not found"
    # 5/09: On some systems, bjobs can return 0, but the job is *not found*.
    #       An additional check for this has been added below.
    if (($exit_code == 255) || (($exit_code == 0) && ($_ eq '')))
    {
        $self->log("bjobs rc is 255 == Job <123> is not found, running bhist");
        # The job was not found. It can also be that it queried
        # LSF too soon: using bhist to determine whether the job
        # was actually submitted, failed or done
        $out = `$bhist -a -l $job_id 2>/dev/null`;

        # get the exit code of the bhist command.
        $exit_code = $? >> 8;

        if ($exit_code eq 0) {
            $self->log("The job actually was submitted, grepping the output to determine status.");
            if (grep(/Done successfully\./, $out)) {
                $state = Globus::GRAM::JobState::DONE;
            }
            elsif (grep(/Exited with exit code /, $out)) {
                $state = Globus::GRAM::JobState::FAILED;
            }
            else {
                $self->log("The job probably newly submitted, reporting PENDING");
                $state = Globus::GRAM::JobState::PENDING;
            }
        } else {
            $self->log("Neither bhist knows anything about the job, exit code $exit_code, reporting FAILED\n");
            $state = Globus::GRAM::JobState::FAILED;
        }
        $self->nfssync( $description->stdout() )
            if $description->stdout() ne '';
        $self->nfssync( $description->stderr() )
            if $description->stderr() ne '';
    }
    else
    {

        # Get 3th field (status)
        $_ = (split(/\s+/))[2];

        if(/PEND/)
        {
            $state = Globus::GRAM::JobState::PENDING;
        }
        elsif(/DONE/)
        {
            $state = Globus::GRAM::JobState::DONE;
	    $self->nfssync( $description->stdout() )
		if $description->stdout() ne '';
	    $self->nfssync( $description->stderr() )
		if $description->stderr() ne '';
        }
        elsif(/USUSP|SSUSP|PSUSP/)
        {
            $state = Globus::GRAM::JobState::SUSPENDED;
        }
        elsif(/RUN/)
        {
            $state = Globus::GRAM::JobState::ACTIVE;
        }
        elsif(/EXIT/)
        {
            $state = Globus::GRAM::JobState::FAILED;
            $self->nfssync( $description->stdout() )
                if $description->stdout() ne '';
            $self->nfssync( $description->stderr() )
                if $description->stderr() ne '';
        }
        elsif(/UNKWN/)
        {
            # We want the JM to ignore this poll and keep the same state
            # as the previous state.  Returning an empty hash will do the job.
            $self->log("bjobs returned the UNKWN state.  Telling JM to ignore this poll");
            return {};
        }
        elsif(/ZOMBI/)
        {
            return Globus::GRAM::Error::LOCAL_SCHEDULER_ERROR();
        }
        else
        {
            # This else is reached by an unknown response from lsf.
            # It could be that LSF was temporarily unavailable, but that it
            # can recover and the submitted job is fine.
            # We want the JM to ignore this poll and keep the same state
            # as the previous state.  Returning an empty hash will do the job.
            $self->log("bjobs returned an unknown response.  Telling JM to ignore this poll ($_) ($exit_code)");
            return {};
        }
    }

    my @acct_info;
    if ($state == Globus::GRAM::JobState::DONE ||
        $state == Globus::GRAM::JobState::FAILED)
    {
       if (defined open(BACCT,$bhist." -l ".$job_id." 2>&1 |"))
       {
          while(<BACCT>)
          {
             chomp(my $line=$_);
             $line =~ s|\\|\\\\|g;
             push(@acct_info,$line);
          }
          close(BACCT);
       }
    }

# NB Accounting information uses literal '\n' to indicate newlines

    return {    JOB_ACCT_INFO => join('\n',@acct_info),
                JOB_STATE     => $state
           }; 
}

sub cancel
{
    my $self = shift;
    my $description = $self->{JobDescription};
    my $job_id = $description->jobid();

    $self->log("cancel job $job_id");
    system("$bkill $job_id >/dev/null 2>/dev/null");
    #$self->fork_and_exec_cmd( $bkill, $job_id );

    if($? == 0)
    {
	return { JOB_STATE => Globus::GRAM::JobState::FAILED };
    }
    return Globus::GRAM::Error::JOB_CANCEL_FAILED();
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
